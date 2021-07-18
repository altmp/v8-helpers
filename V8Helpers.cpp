
#include "cpp-sdk/ICore.h"
#include "V8ResourceImpl.h"
#include "V8Helpers.h"
#include <climits>

bool V8Helpers::TryCatch(const std::function<bool()>& fn)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::Local<v8::Context> context = isolate->GetEnteredOrMicrotaskContext();
	v8::TryCatch tryCatch(isolate);

	alt::IResource* resource = V8ResourceImpl::GetResource(context);

	if (!fn())
	{
		v8::Local<v8::Value> exception = tryCatch.Exception();
		v8::Local<v8::Message> message = tryCatch.Message();

		if (!message.IsEmpty() && !context.IsEmpty())
		{
			v8::MaybeLocal<v8::String> maybeSourceLine = message->GetSourceLine(context);
			v8::Maybe<int32_t> line = message->GetLineNumber(context);
			v8::ScriptOrigin origin = message->GetScriptOrigin();

			if (!origin.ResourceName()->IsUndefined())
			{
				Log::Error << "[V8] Exception at " << resource->GetName() << ":"
					<< *v8::String::Utf8Value(isolate, origin.ResourceName()) << ":" << line.ToChecked() << Log::Endl;

				if (!maybeSourceLine.IsEmpty())
				{
					v8::Local<v8::String> sourceLine = maybeSourceLine.ToLocalChecked();

					if (sourceLine->Length() <= 80)
					{
						Log::Error << "  " << *v8::String::Utf8Value(isolate, sourceLine) << Log::Endl;
					}
					else
					{
						Log::Error << "  " << std::string{ *v8::String::Utf8Value(isolate, sourceLine), 80 } << "..." << Log::Endl;
					}
				}
			}
			else
			{
				Log::Error << "[V8] Exception at " << resource->GetName() << Log::Endl;
			}

			v8::MaybeLocal<v8::Value> stackTrace = tryCatch.StackTrace(context);
			if (!stackTrace.IsEmpty() && stackTrace.ToLocalChecked()->IsString())
			{
				v8::String::Utf8Value stackTraceStr(isolate, stackTrace.ToLocalChecked().As<v8::String>());
				Log::Error << "  " << *stackTraceStr << Log::Endl;
			}
		}
		else if (!exception.IsEmpty())
		{
			Log::Error << "[V8] Exception: "
				<< *v8::String::Utf8Value(isolate, exception) << Log::Endl;
		}
		else
		{
			Log::Error << "[V8] Exception occured" << Log::Endl;
		}

		return false;
	}

	return true;
}

void V8Helpers::RegisterFunc(v8::Local<v8::Object> exports, const std::string& _name, v8::FunctionCallback cb, void* data)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

	v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, _name.data(), v8::NewStringType::kNormal, _name.size()).ToLocalChecked();

	v8::Local<v8::Function> fn = v8::Function::New(ctx, cb, v8::External::New(isolate, data)).ToLocalChecked();
	fn->SetName(name);

	exports->Set(ctx, name, fn);
}

void V8Helpers::FunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
{
	auto fn = static_cast<alt::MValueFunction*>(info.Data().As<v8::External>()->Value());

	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();
	V8ResourceImpl* resource = V8ResourceImpl::Get(ctx);

	Log::Debug << "FunctionCallback " << resource->GetResource()->GetName() << " " << V8ResourceImpl::Get(isolate->GetEnteredOrMicrotaskContext())->GetResource()->GetName() << Log::Endl;

	alt::MValueArgs args;

	for (uint64_t i = 0; i < info.Length(); ++i)
		args.Push(V8Helpers::V8ToMValue(info[i]));

	alt::MValue res = (*fn)->Call(args);

	info.GetReturnValue().Set(V8Helpers::MValueToV8(res));
}

alt::MValue V8Helpers::V8ToMValue(v8::Local<v8::Value> val)
{
	auto& core = alt::ICore::Instance();

	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

	if (val.IsEmpty())
		return core.CreateMValueNone();

	if (val->IsUndefined())
		return core.CreateMValueNone();

	if (val->IsNull())
		return core.CreateMValueNil();

	if (val->IsBoolean())
		return core.CreateMValueBool(val->BooleanValue(isolate));

	if (val->IsInt32())
		return core.CreateMValueInt(val->Int32Value(ctx).ToChecked());

	if (val->IsUint32())
		return core.CreateMValueUInt(val->Uint32Value(ctx).ToChecked());

	if (val->IsBigInt()) 
		return core.CreateMValueInt(val.As<v8::BigInt>()->Int64Value());

	if (val->IsNumber())
		return core.CreateMValueDouble(val->NumberValue(ctx).ToChecked());

	if (val->IsString())
		return core.CreateMValueString(*v8::String::Utf8Value(isolate, val));

	if (val->IsObject())
	{
		if (val->IsArray())
		{
			v8::Local<v8::Array> v8Arr = val.As<v8::Array>();
			alt::MValueList list = core.CreateMValueList(v8Arr->Length());

			for (uint32_t i = 0; i < v8Arr->Length(); ++i)
				list->Set(i, V8ToMValue(v8Arr->Get(ctx, i).ToLocalChecked()));

			return list;
		}
		else if (val->IsFunction())
		{
			v8::Local<v8::Function> v8Func = val.As<v8::Function>();
			return V8ResourceImpl::Get(ctx)->GetFunction(v8Func);
		}
		else if (val->IsArrayBuffer())
		{
			auto v8Buffer = val.As<v8::ArrayBuffer>()->GetBackingStore();
			return core.CreateMValueByteArray((uint8_t*)v8Buffer->Data(), v8Buffer->ByteLength());
		}
		else
		{
			V8ResourceImpl* resource = V8ResourceImpl::Get(ctx);
			v8::Local<v8::Object> v8Obj = val.As<v8::Object>();

			if(resource->IsMValue(v8Obj))
			{
				return static_cast<alt::IMValue*>(v8Obj->GetInternalField(0).As<v8::External>()->Value());
			}
			if (resource->IsVector3(v8Obj))
			{
				v8::Local<v8::Number> x = v8Obj->Get(ctx, V8::Vector3_XKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> y = v8Obj->Get(ctx, V8::Vector3_YKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> z = v8Obj->Get(ctx, V8::Vector3_ZKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();

				return core.CreateMValueVector3(
					alt::Vector3f{
						x->Value(),
						y->Value(),
						z->Value() });
			}
			else if (resource->IsVector2(v8Obj))
			{
				v8::Local<v8::Number> x = v8Obj->Get(ctx, V8::Vector3_XKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> y = v8Obj->Get(ctx, V8::Vector3_YKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();

				return core.CreateMValueVector2(
					alt::Vector2f{
						x->Value(),
						y->Value() });
			}
			else if (resource->IsRGBA(v8Obj))
			{
				v8::Local<v8::Number> r = v8Obj->Get(ctx, V8::RGBA_RKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> g = v8Obj->Get(ctx, V8::RGBA_GKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> b = v8Obj->Get(ctx, V8::RGBA_BKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();
				v8::Local<v8::Number> a = v8Obj->Get(ctx, V8::RGBA_AKey(isolate)).ToLocalChecked()->ToNumber(ctx).ToLocalChecked();

				return core.CreateMValueRGBA(
					alt::RGBA{
						(uint8_t)r->Value(),
						(uint8_t)g->Value(),
						(uint8_t)b->Value(),
						(uint8_t)a->Value() });
			}
			else if (resource->IsBaseObject(v8Obj))
			{
				V8Entity* ent = V8Entity::Get(v8Obj);
				Log::Debug << "Instanceof BaseObject" << Log::Endl;

				if (!ent)
				{
					Log::Error << "Unable to convert base object to MValue because it was destroyed and is now invalid" << Log::Endl;
					return core.CreateMValueNil();
				}
				else
					return core.CreateMValueBaseObject(ent->GetHandle());
			}
			else
			{
				alt::MValueDict dict = core.CreateMValueDict();
				v8::Local<v8::Array> keys = v8Obj->GetOwnPropertyNames(ctx).ToLocalChecked();

				for (uint32_t i = 0; i < keys->Length(); ++i)
				{
					v8::Local<v8::Value> v8Key = keys->Get(ctx, i).ToLocalChecked();
					if (!v8Obj->Get(ctx, v8Key).ToLocalChecked()->IsUndefined())
					{
						std::string key = *v8::String::Utf8Value(isolate, v8Key->ToString(ctx).ToLocalChecked());
						dict->Set(key, V8ToMValue(v8Obj->Get(ctx, v8Key).ToLocalChecked()));
					}
				}

				return dict;
			}
		}
	}

	return core.CreateMValueNone();
}

v8::Local<v8::Value> V8Helpers::MValueToV8(alt::MValueConst val)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

	switch (val->GetType())
	{
	case alt::IMValue::Type::NONE:
		return v8::Undefined(isolate);
	case alt::IMValue::Type::NIL:
		return v8::Null(isolate);
	case alt::IMValue::Type::BOOL:
		return v8::Boolean::New(isolate, val.As<alt::IMValueBool>()->Value());
	case alt::IMValue::Type::INT:
	{
		int64_t _val = val.As<alt::IMValueInt>()->Value();

		if (_val >= INT_MIN && _val <= INT_MAX)
			return v8::Integer::New(isolate, _val);

		return v8::BigInt::New(isolate, _val);
	}
	case alt::IMValue::Type::UINT:
	{
		uint64_t _val = val.As<alt::IMValueUInt>()->Value();

		if (_val <= UINT_MAX)
			return v8::Integer::NewFromUnsigned(isolate, _val);

		return v8::BigInt::New(isolate, _val);
	}
	case alt::IMValue::Type::DOUBLE:
		return v8::Number::New(isolate, val.As<alt::IMValueDouble>()->Value());
	case alt::IMValue::Type::STRING:
		return v8::String::NewFromUtf8(isolate, val.As<alt::IMValueString>()->Value().CStr(), v8::NewStringType::kNormal).ToLocalChecked();
	case alt::IMValue::Type::LIST:
	{
		alt::MValueListConst list = val.As<alt::IMValueList>();
		v8::Local<v8::Array> v8Arr = v8::Array::New(isolate, list->GetSize());

		for (uint32_t i = 0; i < list->GetSize(); ++i)
			v8Arr->Set(ctx, i, MValueToV8(list->Get(i)));

		return v8Arr;
	}
	case alt::IMValue::Type::DICT:
	{
		alt::MValueDictConst dict = val.As<alt::IMValueDict>();
		v8::Local<v8::Object> v8Obj = v8::Object::New(isolate);

		for (auto it = dict->Begin(); it; it = dict->Next())
		{
			v8Obj->Set(ctx, v8::String::NewFromUtf8(isolate, it->GetKey().CStr(), v8::NewStringType::kNormal).ToLocalChecked(), MValueToV8(it->GetValue()));
		}

		return v8Obj;
	}
	case alt::IMValue::Type::BASE_OBJECT:
	{
		alt::Ref<alt::IBaseObject> ref = val.As<alt::IMValueBaseObject>()->Value();
		return V8ResourceImpl::Get(ctx)->GetBaseObjectOrNull(ref);
	}
	case alt::IMValue::Type::FUNCTION:
	{
		alt::MValueFunctionConst fn = val.As<alt::IMValueFunction>();
		v8::Local<v8::External> extFn = v8::External::New(isolate, new alt::MValueFunctionConst(fn));
		return v8::Function::New(ctx, V8Helpers::FunctionCallback, extFn).ToLocalChecked();
	}
	case alt::IMValue::Type::VECTOR3:
		return V8ResourceImpl::Get(ctx)->CreateVector3(val.As<alt::IMValueVector3>()->Value());
	case alt::IMValue::Type::VECTOR2:
		return V8ResourceImpl::Get(ctx)->CreateVector2(val.As<alt::IMValueVector2>()->Value());
	case alt::IMValue::Type::RGBA:
		return V8ResourceImpl::Get(ctx)->CreateRGBA(val.As<alt::IMValueRGBA>()->Value());
	case alt::IMValue::Type::BYTE_ARRAY:
	{
		alt::MValueByteArrayConst buffer = val.As<alt::IMValueByteArray>();
		v8::Local<v8::ArrayBuffer> v8Buffer = v8::ArrayBuffer::New(isolate, buffer->GetSize());
		std::memcpy(v8Buffer->GetBackingStore()->Data(), buffer->GetData(), buffer->GetSize());
		return v8Buffer;
	}
	default:
		Log::Warning << "V8::MValueToV8 Unknown MValue type " << (int)val->GetType() << Log::Endl;
	}

	return v8::Undefined(isolate);
}

void V8Helpers::MValueArgsToV8(alt::MValueArgs args, std::vector<v8::Local<v8::Value>>& v8Args)
{
	for (uint64_t i = 0; i < args.GetSize(); ++i)
		v8Args.push_back(MValueToV8(args[i]));
}

void V8Helpers::SetAccessor(v8::Local<v8::Template> tpl, v8::Isolate* isolate, const char* name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter)
{
	tpl->SetNativeDataProperty(v8::String::NewFromUtf8(isolate, name,
		v8::NewStringType::kInternalized)
		.ToLocalChecked(),
		getter, setter);
}

void V8::DefineOwnProperty(v8::Isolate* isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Object> val, const char* name, v8::Local<v8::Value> value, v8::PropertyAttribute attributes)
{
	val->DefineOwnProperty(ctx, v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked(), value, attributes);
}

void V8::DefineOwnProperty(v8::Isolate* isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Object> val, v8::Local<v8::String> name, v8::Local<v8::Value> value, v8::PropertyAttribute attributes)
{
	val->DefineOwnProperty(ctx, name, value, attributes);
}

void V8::SetAccessor(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter)
{
	tpl->PrototypeTemplate()->SetAccessor(v8::String::NewFromUtf8(isolate, name,
		v8::NewStringType::kInternalized)
		.ToLocalChecked(),
		getter, setter,
		v8::Local<v8::Value>(), v8::AccessControl::DEFAULT,
		setter != nullptr ? v8::PropertyAttribute::None : v8::PropertyAttribute::ReadOnly);
}

void V8::SetMethod(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, v8::FunctionCallback callback)
{
	tpl->PrototypeTemplate()->Set(isolate, name, v8::FunctionTemplate::New(isolate, callback));
}

void V8::SetStaticAccessor(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter)
{
	tpl->SetNativeDataProperty(v8::String::NewFromUtf8(isolate, name,
		v8::NewStringType::kInternalized)
		.ToLocalChecked(),
		getter, setter);
}

void V8::SetStaticMethod(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl, const char* name, v8::FunctionCallback callback)
{
	tpl->Set(isolate, name, v8::FunctionTemplate::New(isolate, callback));
}

v8::Local<v8::Value> V8::Get(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, const char* name)
{
	return obj->Get(ctx, v8::String::NewFromUtf8(ctx->GetIsolate(), name,
		v8::NewStringType::kInternalized)
		.ToLocalChecked())
		.ToLocalChecked();
}

v8::Local<v8::Value> V8::Get(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, v8::Local<v8::Name> name)
{
	return obj->Get(ctx, name).ToLocalChecked();
}

void V8::SetFunction(v8::Isolate* isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Object> target, const char* name, v8::FunctionCallback cb, void* userData)
{
	v8::Local<v8::String> _name = v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked();

	v8::Local<v8::Function> fn = v8::Function::New(ctx, cb, v8::External::New(isolate, userData)).ToLocalChecked();
	fn->SetName(_name);

	target->Set(ctx, _name, fn);
}

v8::Local<v8::Value> V8::New(v8::Isolate* isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Function> constructor, std::vector<v8::Local<v8::Value>>& args)
{
	v8::Local<v8::Value> obj;

	V8Helpers::TryCatch([&] {
		v8::MaybeLocal<v8::Value> maybeObj = constructor->CallAsConstructor(ctx, args.size(), args.data());

		if (maybeObj.IsEmpty())
			return false;

		obj = maybeObj.ToLocalChecked();
		return true;
	});

	return obj;
}

V8::SourceLocation V8::SourceLocation::GetCurrent(v8::Isolate* isolate)
{
	v8::Local<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(isolate, 1);
	if (stackTrace->GetFrameCount() > 0)
	{
		v8::Local<v8::StackFrame> frame = stackTrace->GetFrame(isolate, 0);

		v8::Local<v8::Value> name = frame->GetScriptName();
		if (!name.IsEmpty())
		{
			std::string fileName = *v8::String::Utf8Value(isolate, frame->GetScriptName());
			int line = frame->GetLineNumber();

			return SourceLocation{ std::move(fileName), line };
		}
		else if (frame->IsEval())
		{
			return SourceLocation{ "[eval]", 0 };
		}
	}

	return SourceLocation{ "[unknown]", 0 };
}

V8::SourceLocation::SourceLocation(std::string&& _fileName, int _line) : fileName(_fileName), line(_line)
{
}

v8::Local<v8::String> V8::Vector3_XKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> xKey{ isolate, v8::String::NewFromUtf8(isolate, "x",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return xKey.Get(isolate);
}

v8::Local<v8::String> V8::Vector3_YKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> yKey{ isolate, v8::String::NewFromUtf8(isolate, "y",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return yKey.Get(isolate);
}

v8::Local<v8::String> V8::Vector3_ZKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> zKey{ isolate, v8::String::NewFromUtf8(isolate, "z",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return zKey.Get(isolate);
}

v8::Local<v8::String> V8::RGBA_RKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> rKey{ isolate, v8::String::NewFromUtf8(isolate, "r",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return rKey.Get(isolate);
}

v8::Local<v8::String> V8::RGBA_GKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> gKey{ isolate, v8::String::NewFromUtf8(isolate, "g",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return gKey.Get(isolate);
}

v8::Local<v8::String> V8::RGBA_BKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> bKey{ isolate, v8::String::NewFromUtf8(isolate, "b",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return bKey.Get(isolate);
}

v8::Local<v8::String> V8::RGBA_AKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> aKey{ isolate, v8::String::NewFromUtf8(isolate, "a",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return aKey.Get(isolate);
}

v8::Local<v8::String> V8::Fire_PosKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> aKey{ isolate, v8::String::NewFromUtf8(isolate, "pos",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return aKey.Get(isolate);
}

v8::Local<v8::String> V8::Fire_WeaponKey(v8::Isolate* isolate)
{
	static v8::Persistent<v8::String> aKey{ isolate, v8::String::NewFromUtf8(isolate, "weapon",
																			v8::NewStringType::kInternalized)
														.ToLocalChecked() };

	return aKey.Get(isolate);
}

bool V8::SafeToBoolean(v8::Local<v8::Value> val, v8::Isolate* isolate, bool& out)
{
	out = val->ToBoolean(isolate)->Value();
	return true;
}

bool V8::SafeToInteger(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, int64_t& out)
{
	v8::MaybeLocal maybeVal = val->ToInteger(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Value();
		return true;
	}

	return false;
}

bool V8::SafeToUInt64(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, uint64_t& out)
{
	v8::MaybeLocal maybeVal = val->ToBigInt(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Uint64Value();
		return true;
	}

	return false;
}

bool V8::SafeToInt64(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, int64_t& out)
{
	v8::MaybeLocal maybeVal = val->ToBigInt(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Int64Value();
		return true;
	}

	return false;
}

bool V8::SafeToUInt32(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, uint32_t& out)
{
	v8::MaybeLocal maybeVal = val->ToUint32(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Value();
		return true;
	}

	return false;
}

bool V8::SafeToInt32(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, int32_t& out)
{
	v8::MaybeLocal maybeVal = val->ToInt32(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Value();
		return true;
	}

	return false;
}

bool V8::SafeToNumber(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, double& out)
{
	v8::MaybeLocal maybeVal = val->ToNumber(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked()->Value();
		return true;
	}

	return false;
}

bool V8::SafeToString(v8::Local<v8::Value> val, v8::Isolate* isolate, v8::Local<v8::Context> ctx, alt::String& out)
{
	v8::MaybeLocal maybeVal = val->ToString(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = *v8::String::Utf8Value(isolate, maybeVal.ToLocalChecked());
		return true;
	}

	return false;
}

bool V8::SafeToFunction(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, v8::Local<v8::Function>& out)
{
	if (val->IsFunction())
	{
		out = val.As<v8::Function>();
		return true;
	}

	return false;
}

bool V8::SafeToObject(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, v8::Local<v8::Object>& out)
{
	v8::MaybeLocal maybeVal = val->ToObject(ctx);
	if (!maybeVal.IsEmpty())
	{
		out = maybeVal.ToLocalChecked();
		return true;
	}

	return false;
}

bool V8::SafeToRGBA(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, alt::RGBA& out)
{
	v8::MaybeLocal maybeVal = val->ToObject(ctx);
	if (!maybeVal.IsEmpty())
	{
		v8::Local val = maybeVal.ToLocalChecked();

		uint32_t r, g, b, a;
		if (SafeToUInt32(V8::Get(ctx, val, "r"), ctx, r)
			&& SafeToUInt32(V8::Get(ctx, val, "g"), ctx, g)
			&& SafeToUInt32(V8::Get(ctx, val, "b"), ctx, b)
			&& SafeToUInt32(V8::Get(ctx, val, "a"), ctx, a)
		) {
			out = alt::RGBA{ uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a) };
			return true;
		}
	}

	return false;
}

bool V8::SafeToVector3(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, alt::Vector3f& out)
{
	v8::MaybeLocal maybeVal = val->ToObject(ctx);
	if (!maybeVal.IsEmpty())
	{
		v8::Local val = maybeVal.ToLocalChecked();

		double x, y, z;
		if (SafeToNumber(V8::Get(ctx, val, "x"), ctx, x)
			&& SafeToNumber(V8::Get(ctx, val, "y"), ctx, y)
			&& SafeToNumber(V8::Get(ctx, val, "z"), ctx, z)
		) {
			out = alt::Vector3f{ float(x), float(y), float(z) };
			return true;
		}
	}

	return false;
}

bool V8::SafeToVector2(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, alt::Vector2f& out)
{
	v8::MaybeLocal maybeVal = val->ToObject(ctx);
	if (!maybeVal.IsEmpty())
	{
		v8::Local val = maybeVal.ToLocalChecked();

		double x, y;
		if (SafeToNumber(V8::Get(ctx, val, "x"), ctx, x)
			&& SafeToNumber(V8::Get(ctx, val, "y"), ctx, y)
		) {
			out = alt::Vector2f{ float(x), float(y) };
			return true;
		}
	}

	return false;
}

bool V8::SafeToArrayBuffer(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, v8::Local<v8::ArrayBuffer>& out)
{
	if (val->IsArrayBuffer())
	{
		out = val.As<v8::ArrayBuffer>();
		return true;
	}

	return false;
}

bool V8::SafeToArrayBufferView(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, v8::Local<v8::ArrayBufferView>& out)
{
	if (val->IsArrayBufferView())
	{
		out = val.As<v8::ArrayBufferView>();
		return true;
	}

	return false;
}

bool V8::SafeToArray(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, v8::Local<v8::Array>& out) 
{
	if (val->IsArray())
	{
		out = val.As<v8::Array>();
		return true;
	}

	return false;
}

std::vector<V8::EventCallback *> V8::EventHandler::GetCallbacks(V8ResourceImpl *impl, const alt::CEvent *e)
{
	return callbacksGetter(impl, e);
}

std::vector<v8::Local<v8::Value>> V8::EventHandler::GetArgs(V8ResourceImpl* impl, const alt::CEvent* e)
{
	std::vector<v8::Local<v8::Value>> args;
	argsGetter(impl, e, args);
	return args;
}

V8::EventHandler* V8::EventHandler::Get(const alt::CEvent* e)
{
	auto& _all = all();
	auto it = _all.find(e->GetType());

	return (it != _all.end()) ? it->second : nullptr;
}

void V8::EventHandler::Register(alt::CEvent::Type type, EventHandler* handler)
{
	auto& _all = all();
	if (_all.count(type) == 0)
	{
		_all.insert({ type, handler });
	}
	else
	{
		Log::Error << "Handler for " << (int)type << " already defined" << Log::Endl;
	}
}

V8::EventHandler::CallbacksGetter V8::LocalEventHandler::GetCallbacksGetter(const std::string& name)
{
	return [name](V8ResourceImpl* resource, const alt::CEvent*) -> std::vector<EventCallback*> {
		return resource->GetLocalHandlers(name);
	};
}

V8::EventHandler::EventHandler(alt::CEvent::Type type, CallbacksGetter &&_handlersGetter, ArgsGetter &&_argsGetter)
	: callbacksGetter(std::move(_handlersGetter)), argsGetter(std::move(_argsGetter)), type(type)
{
	Register(type, this);
}

// Temp issue fix for https://stackoverflow.com/questions/9459980/c-global-variable-not-initialized-when-linked-through-static-libraries-but-ok
void V8::EventHandler::Reference() {
	Log::Info << "[V8] Registered handler for " << std::to_string((int)type) << Log::Endl;
}

alt::String V8::Stringify(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx) 
{
	v8::Local<v8::String> str;
	if(!val->ToString(ctx).ToLocal(&str)) return nullptr;
	if (val->IsObject() && strcmp(*v8::String::Utf8Value(ctx->GetIsolate(), str), "[object Object]") == 0) {
		v8::MaybeLocal<v8::String> maybe = v8::JSON::Stringify(ctx, val);
		v8::Local<v8::String> stringified;
		if (maybe.ToLocal(&stringified)) str = stringified;
	}
	return *v8::String::Utf8Value(ctx->GetIsolate(), str);
}

alt::String V8::GetJSValueTypeName(v8::Local<v8::Value> val) 
{
	if(val->IsUndefined()) return "undefined";
	if(val->IsNull()) return "null";
	if(val->IsNumber()) return "number";
	if(val->IsString()) return "string";
	if(val->IsArray()) return "array";
	if(val->IsBoolean()) return "bool";
	if(val->IsBigInt()) return "bigint";
	if(val->IsArrayBuffer()) return "arraybuffer";
	if(val->IsArrayBufferView()) return "arraybufferview";
	if(val->IsDate()) return "date";
	if(val->IsArgumentsObject()) return "arguments";
	if(val->IsAsyncFunction()) return "asyncfunction";
	if(val->IsExternal()) return "external";
	if(val->IsDataView()) return "dataview";
	if(val->IsSymbol()) return "symbol";
	if(val->IsFunction()) return "function";
	if(val->IsRegExp()) return "regexp";
	if(val->IsGeneratorFunction()) return "generatorfunction";
	if(val->IsPromise()) return "promise";
	if(val->IsProxy()) return "proxy";
	if(val->IsMap()) return "map";
	if(val->IsSet()) return "set";
	if(val->IsWeakMap()) return "weakmap";
	if(val->IsWeakSet()) return "weakset";
	if(val->IsTypedArray()) return "typedarray";
	if(val->IsProxy()) return "proxy";
	if(val->IsObject()) return "object";
	else return "unknown";
}
