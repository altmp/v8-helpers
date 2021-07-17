#include "../V8Helpers.h"
#include "../V8ResourceImpl.h"
#include "../V8Class.h"
#include "../V8Entity.h"

static void ValueGetter(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    V8_GET_ISOLATE();

    auto value = info.This()->GetInternalField(0);
    V8_CHECK(!value.IsEmpty(), "MValue is empty");
    V8_RETURN(V8Helpers::MValueToV8(static_cast<alt::IMValue*>(value.As<v8::External>()->Value())));
}

static void TypeGetter(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    V8_GET_ISOLATE();

    auto value = info.This()->GetInternalField(0);
    V8_CHECK(!value.IsEmpty(), "MValue is empty");
    V8_RETURN_ENUM(static_cast<alt::IMValue*>(value.As<v8::External>()->Value())->GetType());
}

static void ValidGetter(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    V8_GET_ISOLATE();

    V8_RETURN_BOOLEAN(!info.This()->GetInternalField(0).IsEmpty());
}

static void Destroy(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    V8_GET_ISOLATE();

    auto value = info.This()->GetInternalField(0);
    V8_CHECK(!value.IsEmpty(), "MValue is empty");
    static_cast<alt::IMValue*>(info.This()->GetInternalField(0).As<v8::External>()->Value())->RemoveRef();
    info.This()->SetInternalField(0, v8::Local<v8::Value>());
}

static void Constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    V8_GET_ISOLATE();
    V8_CHECK_CONSTRUCTOR();
    V8_CHECK_ARGS_LEN(1);

    auto mvalue = V8Helpers::V8ToMValue(info[0]);
    mvalue->AddRef();
    info.This()->SetInternalField(0, v8::External::New(isolate, mvalue.Get()));
}

extern V8Class v8MValue("MValue", Constructor, [](v8::Local<v8::FunctionTemplate> tpl) {
	v8::Isolate *isolate = v8::Isolate::GetCurrent();

    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    V8::SetAccessor(isolate, tpl, "value", &ValueGetter);
    V8::SetAccessor(isolate, tpl, "type", &TypeGetter);
    V8::SetAccessor(isolate, tpl, "valid", &ValidGetter);
    V8::SetMethod(isolate, tpl, "destroy", &Destroy);
});
