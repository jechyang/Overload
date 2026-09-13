#pragma once
#include <OvReflection/Annotations.h>
#include <vector>

// One supported C++ translation unit per CASE; every active annotation must fail.
#if CASE == 1
struct OV_TYPE() Bad { OV_PROPERTY(Setter = SetX) int GetX() const; void SetX(float value); };
#elif CASE == 2
struct OV_TYPE(Id = "same") First {};
struct OV_TYPE(Id = "same") Second {};
#elif CASE == 3
struct OV_TYPE() Bad { OV_FIELD() int* pointer; };
#elif CASE == 4
class OV_TYPE() Bad { OV_FIELD() int secret; };
#elif CASE == 5
enum class OV_TYPE(Component) Bad { X };
#elif CASE == 6
union OV_TYPE() Bad { int a; float b; };
#elif CASE == 7
struct OV_TYPE(Id = "current", Alias = "old") First {};
struct OV_TYPE(Id = "old") Second {};
#elif CASE == 8
struct OV_TYPE() Base { OV_FIELD() int value; };
struct OV_TYPE() Bad : Base { OV_FIELD() float value; };
#elif CASE == 9
struct OV_TYPE() Bad { OV_PROPERTY(ReadOnly) int Read() const; };
#elif CASE == 10
struct OV_TYPE() Bad { OV_PROPERTY(Setter = SetX) int GetX() const; void SetX(int); void SetX(float); };
#elif CASE == 11
struct OV_TYPE() Bad { OV_FIELD() std::vector<int> values; };
#elif CASE == 12
struct OV_TYPE() Bad { OV_FIELD() unsigned int flags : 2; };
#elif CASE == 13
struct OV_TYPE() Bad { OV_FIELD() static int value; };
#elif CASE == 14
struct OV_TYPE() Bad { OV_METHOD() int Run(int); int Run(float); };
#elif CASE == 15
struct OV_TYPE(Id = "x", Id = "y") Bad {};
#elif CASE == 16
struct OV_TYPE(Id = "") Bad {};
#elif CASE == 17
struct OV_TYPE() Bad { OV_PROPERTY(ReadOnly, Setter = SetX) int GetX() const; void SetX(int); };
#elif CASE == 18
struct OV_TYPE() Bad { OV_PROPERTY(Setter = SetX) int GetX() const; void SetX(int&); };
#elif CASE == 19
struct OV_TYPE() Bad { OV_PROPERTY() int GetX() const; };
#elif CASE == 20
struct Bad { OV_FIELD() int value; };
#elif CASE == 21
OV_TYPE() void Bad();
#elif CASE == 22
struct OV_TYPE() First {};
class OV_TYPE() Bad { friend struct OvReflection::Access<First>; OV_FIELD() int value; };
#elif CASE == 23
struct OV_TYPE() Bad { OV_FIELD(Name = "x") int a; OV_FIELD(Name = "x") float b; };
#elif CASE == 24
struct OV_TYPE(Unknown) Bad {};
#elif CASE == 25
struct Base {};
struct OV_TYPE() Bad : virtual Base {};
#elif CASE == 26
struct A {}; struct B {}; struct OV_TYPE() Bad : A, B {};
#elif CASE == 27
template<class T> struct OV_TYPE() Bad {};
#elif CASE == 28
struct OV_TYPE() Bad { template<class T> OV_METHOD() int Run(T); };
#elif CASE == 29
namespace { struct OV_TYPE() Bad {}; }
#elif CASE == 30
inline void Local() { struct OV_TYPE() Bad {}; }
#elif CASE == 31
struct OV_TYPE() Bad;
#elif CASE == 32
struct OV_TYPE(Component, Factory = AddComponent) Bad {};
#elif CASE == 33
enum OV_TYPE(Lookup = Existing) Bad { X };
#elif CASE == 34
struct OV_TYPE() Bad { OV_FIELD() int GetX() const; };
#elif CASE == 35
struct OV_TYPE() Bad { OV_METHOD() int x; };
#elif CASE == 36
struct OV_TYPE() Bad { OV_PROPERTY(Setter = Missing) int GetX() const; };
#elif CASE == 37
struct OV_TYPE() Bad { OV_PROPERTY(Setter = SetX) int GetX() const; int SetX(int); };
#elif CASE == 38
struct OV_TYPE() Bad { OV_PROPERTY(ReadOnly) int GetX(); };
#elif CASE == 39
struct OV_TYPE() Bad { OV_FIELD(Thread = Worker) int x; };
#elif CASE == 40
struct OV_TYPE() Bad { OV_FIELD(Inspect = true) int x; };
#elif CASE == 41
class OV_TYPE() Bad { public: OV_PROPERTY(Setter = SetX) int GetX() const; private: void SetX(int); };
#elif CASE == 42
struct Root {}; struct Base : virtual Root {}; struct OV_TYPE() Bad : Base {};
#elif CASE == 43
struct OV_TYPE() Bad { OV_METHOD() int Run(int); template<class T> int Run(T); };
#elif CASE == 44
OV_TYPE() struct Bad {};
#elif CASE == 45
enum class Hidden { X }; struct OV_TYPE() Bad { OV_FIELD() Hidden value; };
#elif CASE == 46
struct OV_TYPE(Alias = "old", Alias = "old") Bad {};
#elif CASE == 47
struct OV_TYPE() Bad { OV_FIELD(Id = "duplicate") int x; OV_METHOD(Id = "duplicate") void Run(); };
#elif CASE == 48
struct OV_TYPE() Bad { OV_PROPERTY(ReadOnly) int GetX() const &&; };
#elif CASE == 49
struct OV_TYPE() Bad { OV_FIELD() int values[3]; };
#elif CASE == 50
struct OV_TYPE() Bad { OV_METHOD() void Run(int* pointer); };
#elif CASE == 51
struct OV_TYPE() Bad { void Run(); }; OV_METHOD() inline void Bad::Run() {}
#endif
