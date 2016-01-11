/* A tagged union variant class.
   Copyright (C) 2013-2016 Jarryd Beck

This file is part of Juice.

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

// This file is an implementation of the variant in proposal P0088R0 for C++,
// http://www.open-std.org/JTC1/SC22/WG21/docs/papers/2015/p0088r0.pdf
// The needed meta-function tuple_find is implemented in tuple.hpp
// In summary:
//   1. It is almost never empty, and visiting an empty variant is undefined.
//   2. Accessing the wrong element throws. But the variant must still be valid.
//   3. The copy/move assignment operators follow the exception safety of
//      the contained types, and guarantee that the variant will not be empty
//      as long as the last copy or move does not throw.
//   4. It is adapted to have tuple-like access of its types.
//
// This variant is a superset of the standard variant. The two additional
// features are support for recursive types with recursive_wrapper, and
// the visit function can take arbitrary arguments that are passed on to
// the visitor.
//
// == Notes ==
// Hash is not yet implemented.
// Some of the visitors have operator()() lying around from trying a previous
// proposal with empty visitation.


#ifndef JUICE_VARIANT_HPP_INCLUDED
#define JUICE_VARIANT_HPP_INCLUDED

#include <cassert>
#include <functional>
#include <initializer_list>
#include <new>
#include <typeindex>
#include <type_traits>
#include <utility>

#include "conjunction.hpp"
#include "mpl.hpp"
#include "tuple.hpp"

namespace juice
{
  namespace MPL
  {
    struct true_ {};
    struct false_ {};
  }

  template <typename T> struct emplaced_type_t{};
  template <typename T> constexpr emplaced_type_t<T> emplaced_type{};
  template <size_t I> struct emplaced_index_t {};
  template <size_t I> constexpr emplaced_index_t<I> emplaced_index{};

  template <typename R = void>
  class
  [[deprecated("result_type is not used anymore")]]
  static_visitor
  {
    public:
    typedef R result_type;

    //so that it can't be instantiated
    protected:
    ~static_visitor() = default;
  };

  template <typename T>
  class recursive_wrapper
  {
    public:
    ~recursive_wrapper()
    {
      delete m_t;
    }

    template 
    <
      typename U,
      typename Dummy = 
        typename std::enable_if<std::is_convertible<U, T>::value, U>::type
    >
    recursive_wrapper(
      const U& u)
    : m_t(new T(u))
    {
    }

    template 
    <
      typename U,
      typename Dummy =
        typename std::enable_if<std::is_convertible<U, T>::value, U>::type
    >
    recursive_wrapper(U&& u)
    : m_t(new T(std::forward<U>(u))) { }

    recursive_wrapper(const recursive_wrapper& rhs)
    : m_t(new T(rhs.get())) { }

    recursive_wrapper(recursive_wrapper&& rhs)
    : m_t(rhs.m_t)
    {
      rhs.m_t = nullptr;
    }

    recursive_wrapper&
    operator=(const recursive_wrapper& rhs)
    {
      assign(rhs.get());
      return *this;
    }

    recursive_wrapper&
    operator=(recursive_wrapper&& rhs)
    {
      if (this != &rhs)
      {
        const T* tmp = m_t;
        m_t = rhs.m_t;
        rhs.m_t = nullptr;
        delete tmp;
      }
      return *this;
    }

    recursive_wrapper&
    operator=(const T& t)
    {
      assign(t);
      return *this;
    }

    recursive_wrapper&
    operator=(T&& t)
    {
      assign(std::move(t));
      return *this;
    }

    bool
    operator==(const recursive_wrapper& rhs) const
    {
      return *m_t == *rhs.m_t;
    }

    T& get() { return *m_t; }
    const T& get() const { return *m_t; }

    private:
    T* m_t;

    template <typename U>
    void
    assign(U&& u)
    {
      *m_t = std::forward<U>(u);
    }
  };

  template <typename T>
  struct unwrapped_type
  {
    typedef T type;
  };

  template <typename T>
  struct unwrapped_type<recursive_wrapper<T>>
  {
    typedef T type;
  };

  template <typename T>
  using unwrapped_type_t = typename unwrapped_type<T>::type;

  template <typename T>
  const T&
  recursive_unwrap(const recursive_wrapper<T>& r)
  {
    return r.get();
  }

  template <typename T>
  T&
  recursive_unwrap(recursive_wrapper<T>& r)
  {
    return r.get();
  }

  template <typename T>
  const T&
  recursive_unwrap(const T& t)
  {
    return t;
  }

  template <typename T>
  T&
  recursive_unwrap(T& t)
  {
    return t;
  }

  namespace detail
  {
    template <typename T, typename Internal>
    T&
    get_value(T&& t, const Internal&)
    {
      return t;
    }

    template <typename T>
    T&
    get_value(recursive_wrapper<T>& t, const MPL::false_&)
    {
      return t.get();
    }

    template <typename T>
    const T&
    get_value(const recursive_wrapper<T>& t, const MPL::false_&)
    {
      return t.get();
    }

    template <typename Visitor, typename Visitable>
    struct BinaryVisitor
    {
      typedef typename std::remove_reference<Visitor>::type::result_type
        result_type;

      BinaryVisitor(Visitor&& visitor, Visitable&& visitable)
      : v(visitor)
      , visitable(visitable)
      {
      }

      template <typename T>
      result_type
      operator()(T&& t)
      {
        return apply_visitor(v, visitable, std::forward<T>(t));
      }

      private:

      Visitor& v;
      Visitable& visitable;
    };

    struct
    swapper
    {
      template <typename T>
      void
      operator()(T& t, T& rhs)
      {
        std::swap(t, rhs);
      }

      template <typename T, typename U>
      void
      operator()(T&, U&)
      {
        //this should never be called
        assert(false);
      }
    };

  }    

  struct monostate {};

  constexpr bool operator<(const monostate&, const monostate&)
  { return false; }
  constexpr bool operator>(const monostate&, const monostate&)
  { return false; }
  constexpr bool operator<=(const monostate&, const monostate&)
  { return true; }
  constexpr bool operator>=(const monostate&, const monostate&)
  { return true; }
  constexpr bool operator==(const monostate&, const monostate&)
  { return true; }
  constexpr bool operator!=(const monostate&, const monostate&)
  { return false; }

  class bad_variant_access : public std::logic_error
  {
    public:
    explicit bad_variant_access(const std::string& what_arg)
    : std::logic_error(what_arg)
    {
    }

    explicit bad_variant_access(const char* what_arg)
    : std::logic_error(what_arg)
    {
    }
  };

  template 
  <
    typename Internal,
    typename T, 
    typename Storage, 
    typename Visitor, 
    typename... Args
  >
  decltype(auto)
  visitor_caller(Internal&& internal, 
    Storage&& storage, Visitor&& visitor, Args&&... args)
  {
    typedef typename std::conditional
    <
      std::is_const<
        typename std::remove_pointer<
          typename std::remove_reference<Storage>::type
        >::type
      >::value,
      const T,
      T
    >::type ConstType;

    return visitor(detail::get_value(reinterpret_cast<ConstType&&>(*storage),
      internal), std::forward<Args>(args)...);
  }

  template <typename A, typename... B>
  void
  call_deduce(A a, B... b)
  {
  }

  template <typename... Types>
  class variant
  {
    private:

    typedef typename detail::pack_first<Types...>::type First;

    template <typename... AllTypes>
    struct do_visit
    {
      template 
      <
        typename Internal, 
        typename VoidPtrCV, 
        typename Visitor, 
        typename... Args
      >
      decltype(auto)
      operator()
      (
        Internal&& internal,
        size_t which, 
        VoidPtrCV&& storage, 
        Visitor&& visitor,
        Args&&... args
      )
      {
        //typedef decltype(call_deduce(
        //    &visitor_caller<Internal&&, AllTypes,
        //      VoidPtrCV&&, Visitor, Args&&...>...
        //)) result;

        typedef
        decltype (visitor_caller<Internal&&, First, VoidPtrCV&&, Visitor,
          Args&&...>
          (
            std::forward<Internal>(internal),
            std::forward<VoidPtrCV>(storage), 
            std::forward<Visitor>(visitor), 
            std::forward<Args>(args)...
          ))
        result;

        //typedef typename std::remove_reference<Visitor>::type::result_type
        typedef result
          (*whichCaller)(Internal&&, VoidPtrCV&&, Visitor&&, Args&&...);

        //visiting an empty variant is undefined by the proposal
        //if (which == size_t(-1))
        //{
        //  return visitor();
        //}

        static whichCaller callers[sizeof...(AllTypes)] =
          {
            &visitor_caller<Internal&&, AllTypes,
              VoidPtrCV&&, Visitor, Args&&...>...
          }
        ;

        assert(which >= 0 && which < sizeof...(AllTypes));

        return (*callers[which])
          (
            std::forward<Internal>(internal),
            std::forward<VoidPtrCV>(storage), 
            std::forward<Visitor>(visitor), 
            std::forward<Args>(args)...
          );
      }
    };

    template <typename T>
    struct Sizeof
    {
      static constexpr size_t value = sizeof(T);
    };

    template <typename T>
    struct Alignof
    {
      static constexpr size_t value = alignof(T);
    };

    //size = max of size of each thing
    static constexpr size_t m_size = 
      max
      <
        Sizeof,
        Types...
      >::value;

    struct constructor
    {
      constructor(variant& self)
      : m_self(self)
      {
      }

      void
      operator()() const
      {
        //don't do anything if the rhs is empty
      }

      template <typename T>
      void
      operator()(const T& rhs) const
      {
        m_self.construct(rhs);
      }

      private:
      variant& m_self;
    };

    struct move_constructor
    {
      move_constructor(variant& self)
      : m_self(self)
      {
      }

      void
      operator()() const
      {
      }

      template <typename T>
      void
      operator()(T& rhs) const
      {
        m_self.construct(std::move(rhs));
      }

      private:
      variant& m_self;
    };

    struct assigner
    {
      assigner(variant& self, int rhs_which)
      : m_self(self), m_rhs_which(rhs_which)
      {
      }

      void
      operator()()
      {
        //if the right-hand side is empty then we need to
        //destroy the lhs
        m_self.destroy();
      }

      template <typename Rhs>
      void
      operator()(const Rhs& rhs) const
      {
        if (m_self.which() == m_rhs_which)
        {
          //the types are the same, so just assign into the lhs
          *reinterpret_cast<Rhs*>(m_self.address()) = rhs;
        }
        else
        {
          Rhs tmp(rhs);
          m_self.destroy();

          //if this throws, then we are already empty
          m_self.construct(std::move(tmp));
        }
      }

      private:
      variant& m_self;
      size_t m_rhs_which;
    };
    
    struct move_assigner
    {
      move_assigner(variant& self, int rhs_which)
      : m_self(self), m_rhs_which(rhs_which)
      {
      }

      template <typename Rhs>
      void
      operator()(Rhs& rhs) const
      {
        typedef typename std::remove_const<Rhs>::type RhsNoConst;
        if (m_self.which() == m_rhs_which)
        {
          //the types are the same, so just assign into the lhs
          *reinterpret_cast<RhsNoConst*>(m_self.address()) = std::move(rhs);
        }
        else
        {
          //in case rhs is in a subtree of self, we don't want to destroy it
          //first
          //we can move self to a temporary object because rhs can only be
          //the same type as self, which means that it is in a
          //recursive_wrapper, and recursive_wrapper move assignment only
          //copies its pointer

          //if this throws we are ok because tmp will not exist and
          //m_self will still be consistent
          //variant tmp(std::move(m_self));

          //now m_self is empty, if this throws then we are all good
          //m_self.construct(std::move(rhs));

          //the standard proposal does not do this because there are no
          //recursive types, instead it just does:
          m_self.destroy();
          new (&m_self.m_storage) Rhs(std::move(rhs));
        }
      }

      private:
      variant& m_self;
      size_t m_rhs_which;
    };

    struct equality
    {
      equality(const variant& self)
      : m_self(self)
      {
      }

      bool
      operator()() const
      {
        //not equal when something is empty
        return false;
      }

      template <typename Rhs>
      bool
      operator()(Rhs& rhs) const
      {
        return *reinterpret_cast<Rhs*>(m_self.address()) == rhs;
      }

      private:
      const variant& m_self;
    };

    struct destroyer
    {
      void
      operator()() const
      {
        //do nothing when empty
      }

      template <typename T>
      void
      operator()(T& t) const
      {
        t.~T();
      }
    };

    template <typename... MyTypes>
    struct assign_FUN
    {
      static void FUN();
    };

    template <typename Current, typename... MyTypes>
    struct assign_FUN<Current, MyTypes...> : public assign_FUN<MyTypes...>
    {
      using assign_FUN<MyTypes...>::FUN;

      static Current
      FUN(Current);
    };

    template <size_t Which, typename... MyTypes>
    struct initialiser;

    template <size_t Which, typename Current, typename... MyTypes>
    struct initialiser<Which, Current, MyTypes...> 
      : public initialiser<Which+1, MyTypes...>
    {
      typedef initialiser<Which+1, MyTypes...> base;
      using base::initialise;

      static void 
      initialise(variant& v, Current&& current)
      {
        v.construct(std::move(current));
        v.indicate_which(Which);
      }

      static void
      initialise(variant& v, const Current& current)
      {
        v.construct(current);
        v.indicate_which(Which);
      }
    };

    template <size_t Which>
    struct initialiser<Which>
    {
      //this should never match
      void initialise();
    };

    template <typename Current>
    static
    void
    init_construct(variant& v, const Current& t, MPL::true_)
    {
      Current tmp(t);
      v.destroy();
      v.construct(std::move(tmp));
    }

    template <typename Current>
    static
    void
    init_construct(variant& v, const Current& t, MPL::false_)
    {
      v.destroy();
      v.construct(t);
    }

    template <size_t Which, typename... MyTypes>
    struct assign_initialise;

    template <size_t Which, typename Current, typename... MyTypes>
    struct assign_initialise<Which, Current, MyTypes...> 
      : public assign_initialise<Which+1, MyTypes...>
    {
      typedef assign_initialise<Which+1, MyTypes...> base;
      using base::initialise;

      static void 
      initialise(variant& v, Current&& t)
      {
        if (v.index() == Which)
        {
          reinterpret_cast<Current&>(v.m_storage) = std::move(t);
        }
        else
        {
          v.destroy();
          v.construct(std::move(t));
        }
        v.indicate_which(Which);
      }

      static void
      initialise(variant& v, const Current& t)
      {
        if (v.index() == Which)
        {
          *reinterpret_cast<Current*>(&v.m_storage) = t;
        }
        else
        {
          init_construct(v, t,
            typename 
              std::conditional<std::is_move_constructible<Current>::value,
                MPL::true_,
                MPL::false_
              >::type());
        }
        v.indicate_which(Which);
      }
    };

    template <size_t Which>
    struct assign_initialise<Which>
    {
      //this should never match
      void initialise();
    };


    public:

    template <typename = typename
      std::enable_if<
        std::is_default_constructible<First>::value
      >::type
    >
    constexpr
    variant() noexcept(std::is_nothrow_default_constructible<First>::value)
    : m_which(0)
    {
      emplace_internal<First>();
    }

    ~variant()
    {
      destroy();
    }

    //enable_if disables this function if we are constructing with a variant.
    //Unfortunately, this becomes variant(variant&) which is a better match
    //than variant(const variant& rhs), so it is chosen. Therefore, we disable
    //it.
    template 
    <
      typename T, 
      typename Dummy = 
        typename std::enable_if
        <
          !std::is_same
          <
            typename std::remove_reference<variant<Types...>>::type,
            typename std::remove_reference<T>::type
          >::value,
          T
        >::type
    >
    variant(T&& t)
    {
       static_assert(
          !std::is_same<variant<Types...>&, T>::value,
          "why is variant(T&&) instantiated with a variant?");

      //compile error here means that T is not unambiguously convertible to
      //any of the types in (First, Types...)
      //initialiser<0, Types...>::initialise(*this, std::forward<T>(t));
      typedef decltype(assign_FUN<Types...>::FUN(std::forward<T>(t))) type;
      constexpr auto I = tuple_find_v<type, variant>;

      std::cout << "constructing with type " << I << std::endl;
      new (&m_storage) type(std::forward<T>(t));
      indicate_which(I);
    }

    variant(const variant& rhs)
    {
      rhs.apply_visitor_internal(constructor(*this));
      indicate_which(rhs.which());
    }

    variant(variant&& rhs)
    noexcept(conjunction<std::is_nothrow_move_constructible<
      Types
    >::value...>::value)
    {
      //this does not invalidate rhs, it moves the value in rhs to this,
      //which leaves an empty but valid value in rhs
      rhs.apply_visitor_internal(move_constructor(*this));
      indicate_which(rhs.which());
    }

    template <typename T>
    variant(const T& t)
    {
      initialiser<0, Types...>::initialise(*this, t);
    }

    template <typename T, typename... Args>
    explicit variant(emplaced_type_t<T>, Args&&... args)
    {
      emplace_internal<T>(std::forward<Args>(args)...);
      indicate_which(tuple_find<T, variant<Types...>>::value);
    }

    template <typename T, typename U, typename... Args>
    explicit variant(emplaced_type_t<T>,
      std::initializer_list<U> il,
      Args&&... args)
    {
      emplace_internal<T>(il, std::forward<Args>(args)...);
      indicate_which(tuple_find<T, variant<Types...>>::value);
    }

    template <size_t I, typename... Args>
    explicit variant(emplaced_index_t<I>, Args&&... args)
    {
      emplace_internal<typename std::tuple_element<I, variant>::type>(
        std::forward<Args>(args)...);
      indicate_which(I);
    }

    template <size_t I, typename U, typename... Args>
    explicit variant(emplaced_index_t<I>,
      std::initializer_list<U> il,
      Args&&... args)
    {
      emplace_internal<typename std::tuple_element<I, variant>::type>(
        il, std::forward<Args>(args)...);
      indicate_which(I);
    }

    template <typename T, typename... Args>
    void emplace(Args&&... args)
    {
      if (!valueless_by_exception())
      {
        destroy();
      }
      emplace_internal<T>(std::forward<Args>(args)...);
      indicate_which(tuple_find<T, variant>::value);
    }

    template <typename T, typename U, typename... Args>
    void emplace(std::initializer_list<U> il, Args&&... args)
    {
      if (!valueless_by_exception())
      {
        destroy();
      }
      emplace_internal<T>(il, std::forward<Args>(args)...);
      indicate_which(tuple_find<T, variant>::value);
    }

    template <size_t I, typename... Args>
    void emplace(Args&&... args)
    {
      return emplace<typename std::tuple_element<I, variant>::type>(
        std::forward<Args>(args)...
      );
    }

    template <size_t I, typename U, typename... Args>
    void emplace(std::initializer_list<U> il, Args&&... args)
    {
      return emplace<typename std::tuple_element<I, variant>::type>(
        il,
        std::forward<Args>(args)...
      );
    }

    variant& operator=(const variant& rhs)
    {
      if (this != &rhs)
      {
        rhs.apply_visitor_internal(assigner(*this, rhs.which()));
        indicate_which(rhs.which());
      }
      return *this;
    }

    variant& operator=(variant&& rhs) 
    noexcept(
      conjunction<(
        std::is_nothrow_move_constructible<Types>::value &&
        std::is_nothrow_move_assignable<Types>::value
      )...
      >::value
    )
    {
      if (this != &rhs)
      {
        auto w = rhs.which();
        if (w == tuple_not_found)
        {
          if (index() != tuple_not_found)
          {
            destroy();
          }
        }
        else
        {
          rhs.apply_visitor_internal(move_assigner(*this, w));
          indicate_which(w);
        }
      }
      return *this;
    }

#if 0
    template <typename T>
    variant&
    operator=(const T& t)
    {
      assign_initialise<0, Types...>::initialise(*this, t);

      return *this;
    }
#endif

    template <typename T,
      typename = typename
        std::enable_if<!std::is_same<std::decay_t<T>, variant>::value>::type
    >
    variant&
    operator=(T&& t) noexcept(
      conjunction<(
        std::is_nothrow_move_assignable<Types>::value &&
        std::is_nothrow_move_constructible<Types>::value
        )...
      >::value
    )
    {
      typedef decltype(assign_FUN<Types...>::FUN(std::forward<T>(t))) type;
      constexpr auto I = tuple_find_v<type, variant>;

      if (index() != I)
      {
        destroy();
        new (&m_storage) type(std::forward<T>(t));
      }
      else
      {
        reinterpret_cast<type&>(m_storage) = std::forward<T>(t);
      }

      indicate_which(I);

      return *this;
    }

    bool
    operator==(const variant& rhs) const
    {
      if (which() != rhs.which())
      {
        return false;
      }

      return rhs.apply_visitor_internal(equality(*this));
    }

    size_t which() const {return m_which;}

    size_t index() const { return m_which; }

    bool
    valueless_by_exception() const
    {
      return m_which == tuple_not_found;
    }

    template <typename Internal, typename Visitor, typename... Args>
    decltype(auto)
    apply_visitor(Visitor&& visitor, Args&&... args)
    {
      return do_visit<Types...>()(Internal(), m_which, &m_storage,
        std::forward<Visitor>(visitor), std::forward<Args>(args)...);
    }

    template <typename Internal, typename Visitor, typename... Args>
    decltype(auto)
    apply_visitor(Visitor&& visitor, Args&&... args) const
    {
      return do_visit<Types...>()(Internal(), m_which, &m_storage,
        std::forward<Visitor>(visitor), std::forward<Args>(args)...);
    }

    void
    swap(variant& rhs)
    {
      if (m_which == rhs.index())
      {
        apply_visitor(detail::swapper(), *this, rhs);
      }
      else
      {
        swap(*this, rhs);
      }
    }

    template <size_t I>
    //typename std::tuple_element<I, variant<Types...>>::type&
    auto&
    get() const
    {
      if (index() != I)
      {
        throw bad_variant_access("Tuple does not contain requested item");
      }

      return *reinterpret_cast<
        const typename std::tuple_element<I, variant<Types...>>::type*>(
        &m_storage
      );
    }

    template <size_t I>
    //typename std::tuple_element<I, variant<Types...>>::type&
    auto&
    get()
    {
      if (index() != I)
      {
        throw bad_variant_access("Tuple does not contain requested item");
      }

      return reinterpret_cast<
        typename std::tuple_element<I, variant<Types...>>::type&>(
        m_storage
      );
    }

    private:

    typename 
      std::aligned_storage<m_size, max<Alignof, Types...>::value>::type
      m_storage;

    size_t m_which;

    static std::function<void(void*)> m_handlers[1 + sizeof...(Types)];

    void indicate_which(size_t which) {m_which = which;}

    void* address() {return &m_storage;}
    const void* address() const {return &m_storage;}

    template <typename Visitor>
    decltype(auto)
    apply_visitor_internal(Visitor&& visitor)
    {
      return apply_visitor<MPL::true_, Visitor>(std::forward<Visitor>(visitor));
    }

    template <typename Visitor>
    decltype(auto)
    apply_visitor_internal(Visitor&& visitor) const
    {
      return apply_visitor<MPL::true_, Visitor>(std::forward<Visitor>(visitor));
    }

    void
    destroy()
    {
      //shortcut here to bypass calling the empty destroy function
      if (index() != tuple_not_found)
      {
        apply_visitor_internal(destroyer());
        indicate_which(tuple_not_found);
      }
    }

    template <typename T, typename... Args>
    constexpr
    void
    emplace_internal(Args&&... args)
    {
      new(&m_storage) T(std::forward<Args>(args)...);
    }

    template <typename T>
    constexpr
    void
    construct(T&& t)
    {
      typedef typename std::remove_reference<T>::type type;
      new(&m_storage) type(std::forward<T>(t));
    }
  };

  template <typename... Types>
  using Variant = variant<Types...>;

  struct bad_get : public std::exception
  {
    virtual const char* what() const throw()
    {
      return "bad_get";
    }
  };

  template <typename T>
  struct get_visitor
  {
    typedef T* result_type;

    result_type
    operator()()
    {
      throw bad_get();
    }

    result_type
    operator()(T& val) const
    {
      return &val;
    }

    template <typename U>
    result_type
    operator()(const U&) const
    {
      return nullptr;
    }
  };

  template <typename Visitor, typename Visitable, typename... Args>
//#ifdef __has_cpp_attribute
//#if __has_cpp_attribute(deprecated)
  [[ deprecated("Use the general visit function") ]]
//#endif
//#endif
  decltype(auto)
  apply_visitor(Visitor&& visitor, Visitable&& visitable, Args&&... args)
  {
    return visitable.template apply_visitor<MPL::false_>
      (std::forward<Visitor>(visitor), std::forward<Args>(args)...);
  }

//#if __cpp_generic_lambdas >= 201304 && __cpp_decltype_auto >= 201304
  template <typename Visitor>
  auto
  apply_visitor(Visitor&& visitor)
  {
    return [&visitor] (auto... values) -> auto
    {
      return apply_visitor(visitor, values...);
    };
  }
//#endif

  template <typename Visitor, typename... Visited>
  class MultiVisitor
  {
    public:

    constexpr
    MultiVisitor(Visitor&& vis, Visited&&... vs)
    : m_vis(std::forward<Visitor>(vis))
    , m_vs(std::forward<Visited>(vs)...)
    {
    }

    template <typename First, int... I>
    constexpr
    auto
    make_multi(Visitor&& v, std::integer_sequence<int, I...>, First&& f)
    {
      return MultiVisitor<Visitor, Visited..., First>(
        std::forward<Visitor>(v),
        std::get<I>(m_vs)...,
        std::forward<First>(f));
    }

    decltype(auto)
    operator()() const
    {
      //this is an interesting case, if one of the values is empty,
      //what can we return here? We should probably return the empty case
      //of the base visitor
      //although is it interesting for anyone that we allow arbitrary
      //variants in the multi visit to be empty? If we do, then the number
      //of functions needed to handle this grows quickly, a three visitor
      //would require all possible binary and unary cases,
      //although a variadic pack takes care of anything that you don't care
      //about if you only want to cover a few cases.
      return m_vis();
    }

    template <typename First, typename... Values>
    decltype(auto)
    operator()(First&& f, Values&&... values)
    {
      return 
        make_multi(std::forward<Visitor>(m_vis), 
          std::make_integer_sequence<int, sizeof...(Visited)>(), 
          std::forward<First>(f))
        .visit(std::forward<Values>(values)...);
    }

    template <typename... Types, typename... Args>
    decltype(auto)
    visit(const variant<Types...>& var, Args&&... args)
    {
      return var.template
        apply_visitor<MPL::false_>(*this, std::forward<Args>(args)...);
    }

    template <typename... Types, typename... Args>
    decltype(auto)
    visit(variant<Types...>& var, Args&&... args)
    {
      return var.template
        apply_visitor<MPL::false_>(*this, std::forward<Args>(args)...);
    }

    template <typename... Types, typename... Args>
    decltype(auto)
    visit(variant<Types...>&& var, Args&&... args)
    {
      return std::move(var).template
        apply_visitor<MPL::false_>(*this, std::forward<Args>(args)...);
    }

    template <int... I, typename... Args>
    decltype(auto)
    do_visit(std::integer_sequence<int, I...>, Visitor&& v, Args&&... args)
    {
      return std::forward<Visitor>(v)
        (std::get<I>(m_vs)..., std::forward<Args>(args)...);
    }

    template <typename... Args>
    decltype(auto)
    visit(Args&&... args)
    {
      return do_visit(std::make_integer_sequence<int, sizeof...(Visited)>(),
        std::forward<Visitor>(m_vis), std::forward<Args>(args)...);
    }

    private:
    Visitor&& m_vis;
    std::tuple<Visited&...> m_vs;
  };

  template <typename Visitor, typename... Values>
  decltype(auto)
  visit(Visitor&& vis, Values&&... args)
  {
    return MultiVisitor<Visitor>(std::forward<Visitor>(vis)).visit(args...);
  }

  // == variant get ==

  // === first the indexed versions ===

  template <size_t I, typename... Types>
  //typename std::tuple_element<I, variant<Types...>>::type&
  auto&
  get(variant<Types...>& v)
  {
    return recursive_unwrap(v.template get<I>());
  }

  template <size_t I, typename... Types>
  //typename std::tuple_element<I, variant<Types...>>::type&
  auto&
  get(const variant<Types...>& v)
  {
    return recursive_unwrap(v.template get<I>());
  }

  template <size_t I, typename... Types>
  auto&&
  get(variant<Types...>&& v)
  {
    return std::forward<
      std::tuple_element_t<I, variant<Types...>>&&>(recursive_unwrap(get<I>(v)));
  }

  template <size_t I, typename... Types>
  std::remove_reference_t<
    unwrapped_type_t<std::tuple_element_t<I, variant<Types...>>>
  >*
  get(variant<Types...>* v)
  {
    if (v->index() != I)
    {
      return nullptr;
    }

    return &recursive_unwrap(get<I>(*v));
  }

  template <size_t I, typename... Types>
  const
  std::remove_reference_t<
    unwrapped_type_t<std::tuple_element_t<I, variant<Types...>>>
  >*
  get(const variant<Types...>* v)
  {
    if (v->index() != I)
    {
      return nullptr;
    }

    return &recursive_unwrap(get<I>(*v));
  }

  // === then the type versions ===

  template <typename T, typename... Types>
  std::remove_reference_t<T>*
  get(variant<Types...>* var)
  {
    //return visit(get_visitor<T>(), *var);
    return get<tuple_find<T, variant<Types...>>::value>(var);
  }

  template <typename T, typename... Types>
  const std::remove_reference_t<T>*
  get(const variant<Types...>* var)
  {
    //return visit(get_visitor<const T>(), *var);
    return get<tuple_find<T, variant<Types...>>::value>(var);
  }

  template <typename T, typename... Types>
  std::remove_reference_t<T>&
  get (variant<Types...>& var)
  {
    //T* t = visit(get_visitor<T>(), var);
    //if (t == nullptr){throw bad_get();}

    //return *t;
    return get<tuple_find<T, variant<Types...>>::value>(var);
  }

  template <typename T, typename... Types>
  const std::remove_reference_t<T>&
  get (const variant<Types...>& var)
  {
    //const T* t = visit(get_visitor<const T>(), &var);
    //if (t == nullptr) {throw bad_get();}

    //return *t;
    return get<tuple_find<T, variant<Types...>>::value>(var);
  }

  template <typename T, typename... Types>
  T&&
  get(variant<Types...>&& v)
  {
    return get<tuple_find<T, variant<Types...>>::value>(std::move(v));
  }

  struct visitor_applier
  {
    template <typename Visitor, typename Visitable, typename... Args>
    auto
    operator()(Visitor&& visitor, Visitable&& visitable, Args&&... args)
    -> decltype
      (
        visit
        (
          std::forward<Visitor>(visitor),
          std::forward<Visitable>(visitable),
          std::forward<Args>(args)...
        )
      )
    {
      return visit
      (
        std::forward<Visitor>(visitor),
        std::forward<Visitable>(visitable),
        std::forward<Args>(args)...
      );
    }
  };

  template <typename T, typename V>
  bool
  variant_is_type(const V& v)
  {
    return get<T>(&v) != nullptr;
  }

  template <typename T, typename... Types>
  bool holds_alternative(const variant<Types...>& v)
  {
    return variant_is_type<T>(v);
  }

  template 
  <
    typename Visitor,
    typename Visitable1,
    typename Visitable2
  >
#ifdef __has_cpp_attribute
#if __has_cpp_attribute(deprecated) >= 201309
  [[deprecated("Use the general visit function")]]
#endif
#endif
  typename std::remove_reference<Visitor>::type::result_type
  apply_visitor_binary(Visitor&& visitor, Visitable1&& v1, Visitable2&& v2)
  {
    detail::BinaryVisitor<Visitor, Visitable1> v{
      std::forward<Visitor>(visitor), 
      std::forward<Visitable1>(v1)
    };

    return visit(v, std::forward<Visitable2>(v2));
  }


  template <template <typename> class Compare>
  struct RelationalVisitor
  {
    template <typename T, typename U>
    bool
    operator()(const T& t, const U& u) const
    {
      //this one should never be called
      assert(false);
    }

    bool
    operator()() const
    {
      //always false if one is empty
      return false;
    }

    template <typename T>
    bool
    operator()(const T& a, const T& b) const
    {
      return Compare<T>()(a, b);
    }
  };

  template <template <typename> class Compare>
  struct variantCompare
  {
    template <typename... Types>
    bool
    operator()(const variant<Types...>& v, const variant<Types...>& w)
    {
      if (Compare<int>()(v.which(), w.which()))
      {
        return true;
      }
      else if (v.which() == w.which())
      {
        return visit(RelationalVisitor<Compare>(), v, w);
      }
      else
      {
        return false;
      }
    }
  };

  template <typename... Types>
  bool
  operator<(const variant<Types...>& v, const variant<Types...>& w)
  {
    return variantCompare<std::less>()(v, w);
  }

  template <typename... Types>
  bool
  operator>(const variant<Types...>& v, const variant<Types...>& w)
  {
    return variantCompare<std::greater>()(v, w);
  }

  template <typename... Types>
  bool
  operator<=(const variant<Types...>& v, const variant<Types...>& w)
  {
    return !variantCompare<std::greater>()(w, v);
  }

  template <typename... Types>
  bool
  operator>=(const variant<Types...>& v, const variant<Types...>& w)
  {
    return !variantCompare<std::less>()(v, w);
  }
}

namespace std {
  using juice::visit;
  using juice::get;

  namespace detail
  {
    inline
    size_t
    hash_combine(size_t seed, size_t combine)
    {
      return seed ^ (combine + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    }

    struct hash_visitor
    {
      template <typename T>
      size_t
      operator()(const T& t)
      {
        type_index ti(typeid(t));
        size_t h = std::hash<type_index>()(ti);
        h = hash_combine(h, std::hash<T>()(t));

        return h;
      }
    };
  }

  template <typename... Types>
  struct hash<juice::variant<Types...>>
  {
    size_t
    operator()(const juice::variant<Types...>& v)
    {
      return visit(detail::hash_visitor(), v);
    }
  };

  template <>
  struct hash<juice::monostate>
  {
    size_t
    operator()(const juice::monostate&)
    {
      return 47;
    }
  };
}

//this is deprecated as we are moving to a more standard naming convention
//it's a shame I can't mark this namespace name as deprecated
//it will be removed in a later version
namespace Juice = juice;

#endif
