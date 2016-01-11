/* Test file for Juice::variant
   Copyright (C) 2013 Jarryd Beck

Distributed under the Boost Software License, Version 1.0

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

  The copyright notices in the Software and this entire statement, including
  the above license grant, this restriction and the following disclaimer,
  must be included in all copies of the Software, in whole or in part, and
  all derivative works of the Software, unless such copies or derivative
  works are solely in the form of machine-executable object code generated by
  a source language processor.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.

*/

#include <iostream>
#include <string>
#include <memory>

#include <typeinfo>

#include <juice/variant.hpp>

using namespace juice;

struct NotMovable
{
  NotMovable() = default;
  NotMovable(NotMovable&&) = delete;
  NotMovable(const NotMovable&) = default;

  NotMovable&
  operator=(const NotMovable&) = default;
};

typedef variant<int, std::string> MyVariant;

class MyVisitor
{
  public:

  int
  operator()(int a) const
  {
    std::cout << "Visit int " << a << std::endl;
    return a;
  }

  int
  operator()(const std::string& s) const
  {
    return s.size();
  }

  int operator()() const
  {
    return 0;
  }
};

struct MyStruct
{
  MyStruct&
  operator=(MyStruct&& rhs)
  {
    if (this != &rhs)
    {
      x = rhs.x;
    }

    return *this;
  }

  MyStruct()
  {
  }

  MyStruct(MyStruct&&)
  {
  }

  int x;
};

typedef Juice::variant<MyStruct, int, NotMovable> ComplexVariant;

struct Multiple
{
  template <typename A, typename B>
  void
  operator()(A a, B b) const
  {
    std::cout << "Multiple visitor" << std::endl;
    std::cout << typeid(A).name() << std::endl;
    std::cout << "(" << a << ", " << b << ")"
      << std::endl;
  }

  void
  operator()() const
  {
  }
};

void
rvalue(int&&)
{
}

void
foo()
{
  MyVariant a, b;

  bool c = a == b;
  (void)c;

  MyVisitor v;
  visit(v, a);
  visit(MyVisitor(), a);

  MyVariant s("hello");
  MyVariant t("goodbye");

  std::cout << "0 < 0: " << (a < b) << std::endl;
  std::cout << "goodbye < hello: " << (t < s) << std::endl;
  std::cout << "0 < hello: " << (a < s) << std::endl;
  std::cout << "hello < goodbye: " << (s < t) << std::endl;
  std::cout << "goodbye <= hello: " << (s <= t) << std::endl;
  std::cout << "0 <= 0: " << (a <= b) << std::endl;
  std::cout << "0 >= 0: " << (a >= b) << std::endl;

  ComplexVariant complexa;
  //complexa = ComplexVariant(5);
  a = MyVariant();

  //auto delayed = apply_visitor(v);
  //delayed(a);

  Multiple m;
  visit(m, a, s);

  //use empty constructor
  MyVariant d(b);

  //empty assignment
  d = a;

  MyVariant emplaced(emplaced_index_t<1>(), "test");

  std::string moveassign = "moveassign";
  emplaced = "assign";
  emplaced = moveassign;

  NotMovable notm;
  complexa = notm;

  auto& integer = get<0>(a);
  integer = 5;
  std::cout << juice::get<int>(a) << std::endl;

  rvalue(get<0>(MyVariant()));
  rvalue(get<int>(MyVariant()));

  const MyVariant ca(5);
  get<int>(ca);
  get<0>(ca);
  get<0>(&ca);
  get<0>(&a);
}

struct Recursive;

typedef juice::variant<char, int,
  std::unique_ptr<Recursive>>
RVariant;

struct Recursive
{
  RVariant a;
};

typedef juice::variant<char, int&&> RefVariant;

void
bar()
{
  RVariant r(std::unique_ptr<Recursive>(new Recursive{4}));
  auto s = std::move(r);

  int n = 42;
  RefVariant ref(std::move(n));

  std::cout << "Reftype has index: " << ref.index() << std::endl;

  int& m = std::get<int&&>(ref);
  
  std::cout << "ref value = " << m << std::endl;
  m = 5;

  std::cout << "Old n has value " << n << std::endl;
}

int main(int argc, char** argv)
{
  foo();
  bar();
  return 0;
}
