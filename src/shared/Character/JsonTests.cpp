// Tests du mini-parseur JSON. Retourne 0 si OK, non-zéro au premier échec.
#include "src/shared/Character/Json.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

int main()
{
    using engine::json::Value;

    {
        Value v;
        bool ok = engine::json::Parse(R"({"a":1,"b":"x","c":[true,false],"d":{"e":2}})", v);
        Assert(ok, "parse object ok");
        Assert(v.type == Value::Type::Object, "root is object");
        const Value* a = v.Find("a");
        Assert(a && a->type == Value::Type::Number && a->n == 1.0, "a == 1");
        const Value* b = v.Find("b");
        Assert(b && b->type == Value::Type::String && b->s == "x", "b == x");
        const Value* c = v.Find("c");
        Assert(c && c->type == Value::Type::Array && c->a.size() == 2, "c is array[2]");
        Assert(c && c->a[0].type == Value::Type::Bool && c->a[0].b, "c[0] true");
        const Value* d = v.Find("d");
        Assert(d && d->Find("e") && d->Find("e")->n == 2.0, "d.e == 2");
    }
    {
        Value v;
        Assert(!engine::json::Parse("{bad", v), "malformed rejected");
    }
    {
        Value v;
        bool ok = engine::json::Parse(R"(["s1","s2","s3"])", v);
        Assert(ok && v.type == Value::Type::Array && v.a.size() == 3, "string array");
        Assert(v.a[2].type == Value::Type::String && v.a[2].s == "s3", "array[2]==s3");
    }

    return s_fail == 0 ? 0 : 1;
}
