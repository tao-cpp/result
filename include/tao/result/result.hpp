//! \file tao/result/result.hpp
// Tao.Result
//
// Copyright Fernando Pelliccioni 2016-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


#ifndef TAO_RESULT_RESULT_HPP_
#define TAO_RESULT_RESULT_HPP_

#define TAO_OPTIONAL_VERSION_MAJOR 0
#define TAO_OPTIONAL_VERSION_MINOR 2

#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace tao {
#ifndef TAO_MONOSTATE_INPLACE_MUTEX
#define TAO_MONOSTATE_INPLACE_MUTEX
/// \brief Used to represent an optional with no data; essentially a bool
class monostate {};

/// \brief A tag type to tell optional to construct its value in-place
struct in_place_t {
    explicit in_place_t() = default;
};
/// \brief A tag to tell optional to construct its value in-place
static constexpr in_place_t in_place {};
#endif

template <typename T> class optional;

/// \exclude
namespace detail {

// Trait for checking if a type is a tao::optional
template <typename T> struct is_optional_impl : std::false_type {};
template <typename T> struct is_optional_impl<optional<T>> : std::true_type {};
template <typename T> using is_optional = is_optional_impl<decay_t<T>>;

// Change void to tao::monostate
template <typename U>
using fixup_void = conditional_t<std::is_void<U>::value, monostate, U>;

template <typename F, typename U, typename = invoke_result_t<F, U>>
using get_map_return = optional<fixup_void<invoke_result_t<F, U>>>;

// Check if invoking F for some Us returns void
template <typename F, typename = void, typename... U> struct returns_void_impl;
template <typename F, typename... U>
struct returns_void_impl<F, void_t<invoke_result_t<F, U...>>, U...>
    : std::is_void<invoke_result_t<F, U...>> {};
template <typename F, typename... U>
using returns_void = returns_void_impl<F, void, U...>;

template <typename T, typename... U>
using enable_if_ret_void = enable_if_t<returns_void<T &&, U...>::value>;

template <typename T, typename... U>
using disable_if_ret_void = enable_if_t<!returns_void<T &&, U...>::value>;

template <typename T, typename U>
using enable_forward_value =
    detail::enable_if_t<std::is_constructible<T, U&& >::value &&
                        !std::is_same<detail::decay_t<U>, in_place_t>::value &&
                        !std::is_same<optional<T>, detail::decay_t<U>>::value>;

template <typename T, typename U, typename Other>
using enable_from_other = detail::enable_if_t<
    std::is_constructible<T, Other>::value &&
    !std::is_constructible<T, optional<U>&>::value &&
    !std::is_constructible<T, optional<U>&&>::value &&
    !std::is_constructible<T, const optional<U>&>::value &&
    !std::is_constructible<T, const optional<U>&&>::value &&
    !std::is_convertible<optional<U>&, T>::value &&
    !std::is_convertible<optional<U>&&, T>::value &&
    !std::is_convertible<const optional<U>&, T>::value &&
    !std::is_convertible<const optional<U>&&, T>::value>;

template <typename T, typename U>
using enable_assign_forward = detail::enable_if_t<
    !std::is_same<optional<T>, detail::decay_t<U>>::value &&
    !detail::conjunction<std::is_scalar<T>,
                         std::is_same<T, detail::decay_t<U>>>::value &&
    std::is_constructible<T, U>::value && std::is_assignable<T &, U>::value>;

template <typename T, typename U, typename Other>
using enable_assign_from_other = detail::enable_if_t<
    std::is_constructible<T, Other>::value &&
    std::is_assignable<T &, Other>::value &&
    !std::is_constructible<T, optional<U>&>::value &&
    !std::is_constructible<T, optional<U>&&>::value &&
    !std::is_constructible<T, const optional<U>&>::value &&
    !std::is_constructible<T, const optional<U>&&>::value &&
    !std::is_convertible<optional<U>&, T>::value &&
    !std::is_convertible<optional<U>&&, T>::value &&
    !std::is_convertible<const optional<U>&, T>::value &&
    !std::is_convertible<const optional<U>&&, T>::value &&
    !std::is_assignable<T &, optional<U>&>::value &&
    !std::is_assignable<T &, optional<U>&&>::value &&
    !std::is_assignable<T &, const optional<U>&>::value &&
    !std::is_assignable<T &, const optional<U>&&>::value>;


// The storage base manages the actual storage, and correctly propagates
// trivial destruction from T. This case is for when T is not trivially
// destructible.
template <typename T, bool = ::std::is_trivially_destructible<T>::value>
struct optional_storage_base {
    constexpr 
    optional_storage_base() noexcept
        : dummy_(), has_value_(false)
    {}

    template <typename... U>
    constexpr 
    optional_storage_base(in_place_t, U&&... u)
        : value_(std::forward<U>(u)...), has_value_(true)
    {}

    ~optional_storage_base() {
        if (has_value_) {
            value_.~T();
            has_value_ = false;
        }
    }

    struct dummy {};

    union {
        dummy dummy_;
        T value_;
    };

    bool has_value_;
};

// This case is for when T is trivially destructible.
template <typename T> 
struct optional_storage_base<T, true> {
    constexpr 
    optional_storage_base() noexcept
        : dummy_(), has_value_(false) 
    {}

    template <typename... U>
    constexpr 
    optional_storage_base(in_place_t, U&&... u)
        : value_(std::forward<U>(u)...), has_value_(true)
    {}

    // No destructor, so this class is trivially destructible

    struct dummy {};

    union {
        dummy dummy_;
        T value_;
    };

    bool has_value_ = false;
};

// This base class provides some handy member functions which can be used in
// further derived classes
template <typename T> 
struct optional_operations_base : optional_storage_base<T> {
    using optional_storage_base<T>::optional_storage_base;

    void hard_reset() noexcept {
        get().~T();
        this->has_value_ = false;
    }

    template <typename... Args> 
    void construct(Args &&... args) noexcept {
        //TODO(fernando): std::launder() ?
        new (std::addressof(this->value_)) T(std::forward<Args>(args)...);
        this->has_value_ = true;
    }

    template <typename Opt> 
    void assign(Opt &&rhs) {
        if (this->has_value()) {
            if (rhs.has_value()) {
                this->value_ = std::forward<Opt>(rhs).get();
            } else {
                this->value_.~T();
                this->has_value_ = false;
            }
        }

        if (rhs.has_value()) {
            construct(std::forward<Opt>(rhs).get());
        }
    }

    bool has_value() const { return this->has_value_; }

    constexpr 
    T &get() & { return this->value_; }
    
    constexpr 
    const T &get() const & { return this->value_; }
    
    constexpr 
    T &&get() && { return std::move(this->value_); }

#ifndef TAO_OPTIONAL_NO_CONSTRR
    constexpr const T &&get() const && { return std::move(this->value_); }
#endif
};

// This typename manages conditionally having a trivial copy constructor
// This specialization is for when T is trivially copy constructible
template <typename T, bool = TAO_OPTIONAL_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(T)>
struct optional_copy_base : optional_operations_base<T> {
  using optional_operations_base<T>::optional_operations_base;
};

// This specialization is for when T is not trivially copy constructible
template <typename T>
struct optional_copy_base<T, false> : optional_operations_base<T> {
  using optional_operations_base<T>::optional_operations_base;

  optional_copy_base() = default;
  optional_copy_base(const optional_copy_base &rhs) {
    if (rhs.has_value()) {
      this->construct(rhs.get());
    } else {
      this->has_value_ = false;
    }
  }

  optional_copy_base(optional_copy_base &&rhs) = default;
  optional_copy_base &operator=(const optional_copy_base &rhs) = default;
  optional_copy_base &operator=(optional_copy_base &&rhs) = default;
};

// This class manages conditionally having a trivial move constructor
// Unfortunately there's no way to achieve this in GCC < 5 AFAIK, since it
// doesn't implement an analogue to std::is_trivially_move_constructible. We
// have to make do with a non-trivial move constructor even if T is trivially
// move constructible
#ifndef TAO_OPTIONAL_GCC49
template <typename T, bool = std::is_trivially_move_constructible<T>::value>
struct optional_move_base : optional_copy_base<T> {
  using optional_copy_base<T>::optional_copy_base;
};
#else
template <typename T, bool = false> struct optional_move_base;
#endif
template <typename T> struct optional_move_base<T, false> : optional_copy_base<T> {
  using optional_copy_base<T>::optional_copy_base;

  optional_move_base() = default;
  optional_move_base(const optional_move_base &rhs) = default;

  optional_move_base(optional_move_base &&rhs) noexcept(
      std::is_nothrow_move_constructible<T>::value) {
    if (rhs.has_value()) {
      this->construct(std::move(rhs.get()));
    } else {
      this->has_value_ = false;
    }
  }
  optional_move_base &operator=(const optional_move_base &rhs) = default;
  optional_move_base &operator=(optional_move_base &&rhs) = default;
};

// This class manages conditionally having a trivial copy assignment operator
template <typename T, bool = TAO_OPTIONAL_IS_TRIVIALLY_COPY_ASSIGNABLE(T) &&
                          TAO_OPTIONAL_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(T) &&
                          TAO_OPTIONAL_IS_TRIVIALLY_DESTRUCTIBLE(T)>
struct optional_copy_assign_base : optional_move_base<T> {
  using optional_move_base<T>::optional_move_base;
};

template <typename T>
struct optional_copy_assign_base<T, false> : optional_move_base<T> {
  using optional_move_base<T>::optional_move_base;

  optional_copy_assign_base() = default;
  optional_copy_assign_base(const optional_copy_assign_base &rhs) = default;

  optional_copy_assign_base(optional_copy_assign_base &&rhs) = default;
  optional_copy_assign_base &operator=(const optional_copy_assign_base &rhs) {
    this->assign(rhs);
    return *this;
  }
  optional_copy_assign_base &
  operator=(optional_copy_assign_base &&rhs) = default;
};

// This class manages conditionally having a trivial move assignment operator
// Unfortunately there's no way to achieve this in GCC < 5 AFAIK, since it
// doesn't implement an analogue to std::is_trivially_move_assignable. We have
// to make do with a non-trivial move assignment operator even if T is trivially
// move assignable
#ifndef TAO_OPTIONAL_GCC49
template <typename T, bool = std::is_trivially_destructible<T>::value
                       &&std::is_trivially_move_constructible<T>::value
                           &&std::is_trivially_move_assignable<T>::value>
struct optional_move_assign_base : optional_copy_assign_base<T> {
  using optional_copy_assign_base<T>::optional_copy_assign_base;
};
#else
template <typename T, bool = false> struct optional_move_assign_base;
#endif

template <typename T>
struct optional_move_assign_base<T, false> : optional_copy_assign_base<T> {
  using optional_copy_assign_base<T>::optional_copy_assign_base;

  optional_move_assign_base() = default;
  optional_move_assign_base(const optional_move_assign_base &rhs) = default;

  optional_move_assign_base(optional_move_assign_base &&rhs) = default;

  optional_move_assign_base &
  operator=(const optional_move_assign_base &rhs) = default;

  optional_move_assign_base &
  operator=(optional_move_assign_base &&rhs) noexcept(
      std::is_nothrow_move_constructible<T>::value
          &&std::is_nothrow_move_assignable<T>::value) {
    this->assign(std::move(rhs));
    return *this;
  }
};

// optional_delete_ctor_base will conditionally delete copy and move
// constructors depending on whether T is copy/move constructible
template <typename T, bool EnableCopy = std::is_copy_constructible<T>::value,
          bool EnableMove = std::is_move_constructible<T>::value>
struct optional_delete_ctor_base {
  optional_delete_ctor_base() = default;
  optional_delete_ctor_base(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base(optional_delete_ctor_base &&) noexcept = default;
  optional_delete_ctor_base &
  operator=(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base &
  operator=(optional_delete_ctor_base &&) noexcept = default;
};

template <typename T> struct optional_delete_ctor_base<T, true, false> {
  optional_delete_ctor_base() = default;
  optional_delete_ctor_base(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base(optional_delete_ctor_base &&) noexcept = delete;
  optional_delete_ctor_base &
  operator=(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base &
  operator=(optional_delete_ctor_base &&) noexcept = default;
};

template <typename T> struct optional_delete_ctor_base<T, false, true> {
  optional_delete_ctor_base() = default;
  optional_delete_ctor_base(const optional_delete_ctor_base &) = delete;
  optional_delete_ctor_base(optional_delete_ctor_base &&) noexcept = default;
  optional_delete_ctor_base &
  operator=(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base &
  operator=(optional_delete_ctor_base &&) noexcept = default;
};

template <typename T> struct optional_delete_ctor_base<T, false, false> {
  optional_delete_ctor_base() = default;
  optional_delete_ctor_base(const optional_delete_ctor_base &) = delete;
  optional_delete_ctor_base(optional_delete_ctor_base &&) noexcept = delete;
  optional_delete_ctor_base &
  operator=(const optional_delete_ctor_base &) = default;
  optional_delete_ctor_base &
  operator=(optional_delete_ctor_base &&) noexcept = default;
};

// optional_delete_assign_base will conditionally delete copy and move
// constructors depending on whether T is copy/move constructible + assignable
template <typename T,
          bool EnableCopy = (std::is_copy_constructible<T>::value &&
                             std::is_copy_assignable<T>::value),
          bool EnableMove = (std::is_move_constructible<T>::value &&
                             std::is_move_assignable<T>::value)>
struct optional_delete_assign_base {
  optional_delete_assign_base() = default;
  optional_delete_assign_base(const optional_delete_assign_base &) = default;
  optional_delete_assign_base(optional_delete_assign_base &&) noexcept =
      default;
  optional_delete_assign_base &
  operator=(const optional_delete_assign_base &) = default;
  optional_delete_assign_base &
  operator=(optional_delete_assign_base &&) noexcept = default;
};

template <typename T> struct optional_delete_assign_base<T, true, false> {
  optional_delete_assign_base() = default;
  optional_delete_assign_base(const optional_delete_assign_base &) = default;
  optional_delete_assign_base(optional_delete_assign_base &&) noexcept =
      default;
  optional_delete_assign_base &
  operator=(const optional_delete_assign_base &) = default;
  optional_delete_assign_base &
  operator=(optional_delete_assign_base &&) noexcept = delete;
};

template <typename T> struct optional_delete_assign_base<T, false, true> {
  optional_delete_assign_base() = default;
  optional_delete_assign_base(const optional_delete_assign_base &) = default;
  optional_delete_assign_base(optional_delete_assign_base &&) noexcept =
      default;
  optional_delete_assign_base &
  operator=(const optional_delete_assign_base &) = delete;
  optional_delete_assign_base &
  operator=(optional_delete_assign_base &&) noexcept = default;
};

template <typename T> struct optional_delete_assign_base<T, false, false> {
  optional_delete_assign_base() = default;
  optional_delete_assign_base(const optional_delete_assign_base &) = default;
  optional_delete_assign_base(optional_delete_assign_base &&) noexcept =
      default;
  optional_delete_assign_base &
  operator=(const optional_delete_assign_base &) = delete;
  optional_delete_assign_base &
  operator=(optional_delete_assign_base &&) noexcept = delete;
};

} // namespace detail

/// \brief A tag type to represent an empty optional
struct nullopt_t {
  struct do_not_use {};
  constexpr explicit nullopt_t(do_not_use, do_not_use) noexcept {}
};
/// \brief Represents an empty optional
/// \synopsis static constexpr nullopt_t nullopt;
///
/// *Examples*:
/// ```
/// tao::optional<int> a = tao::nullopt;
/// void foo (tao::optional<int>);
/// foo(tao::nullopt); //pass an empty optional
/// ```
static constexpr nullopt_t nullopt{nullopt_t::do_not_use{},
                                   nullopt_t::do_not_use{}};

class bad_optional_access : public std::exception {
public:
  bad_optional_access() = default;
  const char *what() const noexcept { return "Optional has no value"; }
};

/// An optional object is an object that contains the storage for another
/// object and manages the lifetime of this contained object, if any. The
/// contained object may be initialized after the optional object has been
/// initialized, and may be destroyed before the optional object has been
/// destroyed. The initialization state of the contained object is tracked by
/// the optional object.
template <typename T>
class optional : private detail::optional_move_assign_base<T>,
                 private detail::optional_delete_ctor_base<T>,
                 private detail::optional_delete_assign_base<T> {
  using base = detail::optional_move_assign_base<T>;

  static_assert(!std::is_same<T, in_place_t>::value,
                "instantiation of optional with in_place_t is ill-formed");
  static_assert(!std::is_same<detail::decay_t<T>, nullopt_t>::value,
                "instantiation of optional with nullopt_t is ill-formed");

public:
// The different versions for C++14 and 11 are needed because deduced return
// types are not SFINAE-safe. This provides better support for things like
// generic lambdas. C.f.
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0826r0.html
#if defined(TAO_OPTIONAL_CXX14) && !defined(TAO_OPTIONAL_GCC49) &&               \
    !defined(TAO_OPTIONAL_GCC54) && !defined(TAO_OPTIONAL_GCC55)
  /// \group and_then
  /// Carries out some operation which returns an optional on the stored
  /// object if there is one. \requires `std::invoke(std::forward<F>(f),
  /// value())` returns a `std::optional<U>` for some `U`. \returns Let `U` be
  /// the result of `std::invoke(std::forward<F>(f), value())`. Returns a
  /// `std::optional<U>`. The return value is empty if `*this` is empty,
  /// otherwise the return value of `std::invoke(std::forward<F>(f), value())`
  /// is returned.
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &;
  template <typename F> constexpr auto and_then(F &&f) & {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &&;
  template <typename F> constexpr auto and_then(F &&f) && {
    using result = detail::invoke_result_t<F, T &&>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &;
  template <typename F> constexpr auto and_then(F &&f) const & {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &&;
  template <typename F> constexpr auto and_then(F &&f) const && {
    using result = detail::invoke_result_t<F, const T &&>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : result(nullopt);
  }
#endif
#else
  /// \group and_then
  /// Carries out some operation which returns an optional on the stored
  /// object if there is one. \requires `std::invoke(std::forward<F>(f),
  /// value())` returns a `std::optional<U>` for some `U`.
  /// \returns Let `U` be the result of `std::invoke(std::forward<F>(f),
  /// value())`. Returns a `std::optional<U>`. The return value is empty if
  /// `*this` is empty, otherwise the return value of
  /// `std::invoke(std::forward<F>(f), value())` is returned.
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &;
  template <typename F>
  constexpr detail::invoke_result_t<F, T &> and_then(F &&f) & {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &&;
  template <typename F>
  constexpr detail::invoke_result_t<F, T &&> and_then(F &&f) && {
    using result = detail::invoke_result_t<F, T &&>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &;
  template <typename F>
  constexpr detail::invoke_result_t<F, const T &> and_then(F &&f) const & {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &&;
  template <typename F>
  constexpr detail::invoke_result_t<F, const T &&> and_then(F &&f) const && {
    using result = detail::invoke_result_t<F, const T &&>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : result(nullopt);
  }
#endif
#endif

#if defined(TAO_OPTIONAL_CXX14) && !defined(TAO_OPTIONAL_GCC49) &&               \
    !defined(TAO_OPTIONAL_GCC54) && !defined(TAO_OPTIONAL_GCC55)
  /// \brief Carries out some operation on the stored object if there is one.
  /// \returns Let `U` be the result of `std::invoke(std::forward<F>(f),
  /// value())`. Returns a `std::optional<U>`. The return value is empty if
  /// `*this` is empty, otherwise an `optional<U>` is constructed from the
  /// return value of `std::invoke(std::forward<F>(f), value())` and is
  /// returned.
  ///
  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) &;
  template <typename F> constexpr auto map(F &&f) & {
    return optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) &&;
  template <typename F> constexpr auto map(F &&f) && {
    return optional_map_impl(std::move(*this), std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) const&;
  template <typename F> constexpr auto map(F &&f) const & {
    return optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) const&&;
  template <typename F> constexpr auto map(F &&f) const && {
    return optional_map_impl(std::move(*this), std::forward<F>(f));
  }
#else
  /// \brief Carries out some operation on the stored object if there is one.
  /// \returns Let `U` be the result of `std::invoke(std::forward<F>(f),
  /// value())`. Returns a `std::optional<U>`. The return value is empty if
  /// `*this` is empty, otherwise an `optional<U>` is constructed from the
  /// return value of `std::invoke(std::forward<F>(f), value())` and is
  /// returned.
  ///
  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) &;
  template <typename F>
  constexpr decltype(optional_map_impl(std::declval<optional &>(),
                                             std::declval<F &&>()))
  map(F &&f) & {
    return optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) &&;
  template <typename F>
  constexpr decltype(optional_map_impl(std::declval<optional &&>(),
                                             std::declval<F &&>()))
  map(F &&f) && {
    return optional_map_impl(std::move(*this), std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) const&;
  template <typename F>
  constexpr decltype(optional_map_impl(std::declval<const optional &>(),
                              std::declval<F &&>()))
  map(F &&f) const & {
    return optional_map_impl(*this, std::forward<F>(f));
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) const&&;
  template <typename F>
  constexpr decltype(optional_map_impl(std::declval<const optional &&>(),
                              std::declval<F &&>()))
  map(F &&f) const && {
    return optional_map_impl(std::move(*this), std::forward<F>(f));
  }
#endif
#endif

  /// \brief Calls `f` if the optional is empty
  /// \requires `std::invoke_result_t<F>` must be void or convertible to
  /// `optional<T>`.
  /// \effects If `*this` has a value, returns `*this`.
  /// Otherwise, if `f` returns `void`, calls `std::forward<F>(f)` and returns
  /// `std::nullopt`. Otherwise, returns `std::forward<F>(f)()`.
  ///
  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) &;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) & {
    if (has_value())
      return *this;

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) & {
    return has_value() ? *this : std::forward<F>(f)();
  }

  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) &&;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) && {
    if (has_value())
      return std::move(*this);

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) && {
    return has_value() ? std::move(*this) : std::forward<F>(f)();
  }

  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) const &;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const & {
    if (has_value())
      return *this;

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) const & {
    return has_value() ? *this : std::forward<F>(f)();
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \exclude
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const && {
    if (has_value())
      return std::move(*this);

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const && {
    return has_value() ? std::move(*this) : std::forward<F>(f)();
  }
#endif

  /// \brief Maps the stored value with `f` if there is one, otherwise returns
  /// `u`.
  ///
  /// \details If there is a value stored, then `f` is called with `**this`
  /// and the value is returned. Otherwise `u` is returned.
  ///
  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u);
  }

  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u);
  }

  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) const & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) const && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u);
  }
#endif

  /// \brief Maps the stored value with `f` if there is one, otherwise calls
  /// `u` and returns the result.
  ///
  /// \details If there is a value stored, then `f` is
  /// called with `**this` and the value is returned. Otherwise
  /// `std::forward<U>(u)()` is returned.
  ///
  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u) &;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u)();
  }

  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// &&;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u)();
  }

  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// const &;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) const & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u)();
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// const &&;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) const && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u)();
  }
#endif

  /// \returns `u` if `*this` has a value, otherwise an empty optional.
  template <typename U>
  constexpr optional<typename std::decay<U>::type> conjunction(U&& u) const {
    using result = optional<detail::decay_t<U>>;
    return has_value() ? result{u} : result{nullopt};
  }

  /// \returns `rhs` if `*this` is empty, otherwise the current value.
  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) & {
    return has_value() ? *this : rhs;
  }

  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) const & {
    return has_value() ? *this : rhs;
  }

  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) && {
    return has_value() ? std::move(*this) : rhs;
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) const && {
    return has_value() ? std::move(*this) : rhs;
  }
#endif

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) & {
    return has_value() ? *this : std::move(rhs);
  }

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) const & {
    return has_value() ? *this : std::move(rhs);
  }

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) && {
    return has_value() ? std::move(*this) : std::move(rhs);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) const && {
    return has_value() ? std::move(*this) : std::move(rhs);
  }
#endif

  /// Takes the value out of the optional, leaving it empty
  /// \group take
  optional take() & {
    optional ret = *this;
    reset();
    return ret;
  }

  /// \group take
  optional take() const & {
    optional ret = *this;
    reset();
    return ret;
  }

  /// \group take
  optional take() && {
    optional ret = std::move(*this);
    reset();
    return ret;
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group take
  optional take() const && {
    optional ret = std::move(*this);
    reset();
    return ret;
  }
#endif

  using value_type = T;

  /// Constructs an optional that does not contain a value.
  /// \group ctor_empty
  constexpr optional() noexcept = default;

  /// \group ctor_empty
  constexpr optional(nullopt_t) noexcept {}

  /// Copy constructor
  ///
  /// If `rhs` contains a value, the stored value is direct-initialized with
  /// it. Otherwise, the constructed optional is empty.
  constexpr optional(const optional &rhs) = default;

  /// Move constructor
  ///
  /// If `rhs` contains a value, the stored value is direct-initialized with
  /// it. Otherwise, the constructed optional is empty.
  constexpr optional(optional &&rhs) = default;

  /// Constructs the stored value in-place using the given arguments.
  /// \group in_place
  /// \synopsis template <typename... Args> constexpr explicit optional(in_place_t, Args&&... args);
  template <typename... Args>
  constexpr explicit optional(
      detail::enable_if_t<std::is_constructible<T, Args...>::value, in_place_t>,
      Args &&... args)
      : base(in_place, std::forward<Args>(args)...) {}

  /// \group in_place
  /// \synopsis template <typename U, typename... Args>\nconstexpr explicit optional(in_place_t, std::initializer_list<U>&, Args&&... args);
  template <typename U, typename... Args>
  constexpr explicit optional(
      detail::enable_if_t<std::is_constructible<T, std::initializer_list<U>&,
                                                Args &&...>::value,
                          in_place_t>,
      std::initializer_list<U> il, Args &&... args) {
    this->construct(il, std::forward<Args>(args)...);
  }

  /// Constructs the stored value with `u`.
  /// \synopsis template <typename U=T> constexpr optional(U&& u);
  template <
      typename U = T,
      detail::enable_if_t<std::is_convertible<U&&, T>::value> * = nullptr,
      detail::enable_forward_value<T, U> * = nullptr>
  constexpr optional(U&& u) : base(in_place, std::forward<U>(u)) {}

  /// \exclude
  template <
      typename U = T,
      detail::enable_if_t<!std::is_convertible<U&&, T>::value> * = nullptr,
      detail::enable_forward_value<T, U> * = nullptr>
  constexpr explicit optional(U&& u) : base(in_place, std::forward<U>(u)) {}

  /// Converting copy constructor.
  /// \synopsis template <typename U> optional(const optional<U>& rhs);
  template <
      typename U, detail::enable_from_other<T, U, const U &> * = nullptr,
      detail::enable_if_t<std::is_convertible<const U &, T>::value> * = nullptr>
  optional(const optional<U>& rhs) {
    this->construct(*rhs);
  }

  /// \exclude
  template <typename U, detail::enable_from_other<T, U, const U &> * = nullptr,
            detail::enable_if_t<!std::is_convertible<const U &, T>::value> * =
                nullptr>
  explicit optional(const optional<U>& rhs) {
    this->construct(*rhs);
  }

  /// Converting move constructor.
  /// \synopsis template <typename U> optional(optional<U>&& rhs);
  template <
      typename U, detail::enable_from_other<T, U, U&&> * = nullptr,
      detail::enable_if_t<std::is_convertible<U&& , T>::value> * = nullptr>
  optional(optional<U>&& rhs) {
    this->construct(std::move(*rhs));
  }

  /// \exclude
  template <
      typename U, detail::enable_from_other<T, U, U&&> * = nullptr,
      detail::enable_if_t<!std::is_convertible<U&& , T>::value> * = nullptr>
  explicit optional(optional<U>&& rhs) {
    this->construct(std::move(*rhs));
  }

  /// Destroys the stored value if there is one.
  ~optional() = default;

  /// Assignment to empty.
  ///
  /// Destroys the current value if there is one.
  optional &operator=(nullopt_t) noexcept {
    if (has_value()) {
      this->value_.~T();
      this->has_value_ = false;
    }

    return *this;
  }

  /// Copy assignment.
  ///
  /// Copies the value from `rhs` if there is one. Otherwise resets the stored
  /// value in `*this`.
  optional &operator=(const optional &rhs) = default;

  /// Move assignment.
  ///
  /// Moves the value from `rhs` if there is one. Otherwise resets the stored
  /// value in `*this`.
  optional &operator=(optional &&rhs) = default;

  /// Assigns the stored value from `u`, destroying the old value if there was
  /// one.
  /// \synopsis optional &operator=(U&& u);
  template <typename U = T, detail::enable_assign_forward<T, U> * = nullptr>
  optional &operator=(U&& u) {
    if (has_value()) {
      this->value_ = std::forward<U>(u);
    } else {
      this->construct(std::forward<U>(u));
    }

    return *this;
  }

  /// Converting copy assignment operator.
  ///
  /// Copies the value from `rhs` if there is one. Otherwise resets the stored
  /// value in `*this`.
  /// \synopsis optional &operator=(const optional<U>&  rhs);
  template <typename U,
            detail::enable_assign_from_other<T, U, const U &> * = nullptr>
  optional &operator=(const optional<U>& rhs) {
    if (has_value()) {
      if (rhs.has_value()) {
        this->value_ = *rhs;
      } else {
        this->hard_reset();
      }
    }

    if (rhs.has_value()) {
      this->construct(*rhs);
    }

    return *this;
  }

  // TODO check exception guarantee
  /// Converting move assignment operator.
  ///
  /// Moves the value from `rhs` if there is one. Otherwise resets the stored
  /// value in `*this`.
  /// \synopsis optional &operator=(optional<U>&&  rhs);
  template <typename U, detail::enable_assign_from_other<T, U, U> * = nullptr>
  optional &operator=(optional<U>&& rhs) {
    if (has_value()) {
      if (rhs.has_value()) {
        this->value_ = std::move(*rhs);
      } else {
        this->hard_reset();
      }
    }

    if (rhs.has_value()) {
      this->construct(std::move(*rhs));
    }

    return *this;
  }

  /// Constructs the value in-place, destroying the current one if there is
  /// one.
  /// \group emplace
  template <typename... Args> T &emplace(Args &&... args) {
    static_assert(std::is_constructible<T, Args &&...>::value,
                  "T must be constructible with Args");

    *this = nullopt;
    this->construct(std::forward<Args>(args)...);
    return value();
  }

  /// \group emplace
  /// \synopsis template <typename U, typename... Args>\nT& emplace(std::initializer_list<U> il, Args &&... args);
  template <typename U, typename... Args>
  detail::enable_if_t<
      std::is_constructible<T, std::initializer_list<U>&, Args &&...>::value,
      T &>
  emplace(std::initializer_list<U> il, Args &&... args) {
    *this = nullopt;
    this->construct(il, std::forward<Args>(args)...);
    return value();    
  }

  /// Swaps this optional with the other.
  ///
  /// If neither optionals have a value, nothing happens.
  /// If both have a value, the values are swapped.
  /// If one has a value, it is moved to the other and the movee is left
  /// valueless.
  void
  swap(optional &rhs) noexcept(std::is_nothrow_move_constructible<T>::value
                                   &&detail::is_nothrow_swappable<T>::value) {
    if (has_value()) {
      if (rhs.has_value()) {
        using std::swap;
        swap(**this, *rhs);
      } else {
        new (std::addressof(rhs.value_)) T(std::move(this->value_));
        this->value_.T::~T();
      }
    } else if (rhs.has_value()) {
      new (std::addressof(this->value_)) T(std::move(rhs.value_));
      rhs.value_.T::~T();
    }
  }

  /// \returns a pointer to the stored value
  /// \requires a value is stored
  /// \group pointer
  /// \synopsis constexpr const T *operator->() const;
  constexpr const T *operator->() const {
    return std::addressof(this->value_);
  }

  /// \group pointer
  /// \synopsis constexpr T *operator->();
  constexpr T *operator->() {
    return std::addressof(this->value_);
  }

  /// \returns the stored value
  /// \requires a value is stored
  /// \group deref
  /// \synopsis constexpr T &operator*();
  constexpr T &operator*() & { return this->value_; }

  /// \group deref
  /// \synopsis constexpr const T &operator*() const;
  constexpr const T &operator*() const & { return this->value_; }

  /// \exclude
  constexpr T &&operator*() && {
    return std::move(this->value_);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \exclude
  constexpr const T &&operator*() const && { return std::move(this->value_); }
#endif

  /// \returns whether or not the optional has a value
  /// \group has_value
  constexpr bool has_value() const noexcept { return this->has_value_; }

  /// \group has_value
  constexpr explicit operator bool() const noexcept {
    return this->has_value_;
  }

  /// \returns the contained value if there is one, otherwise throws
  /// [bad_optional_access]
  /// \group value
  /// \synopsis constexpr T &value();
  constexpr T &value() & {
    if (has_value())
      return this->value_;
    throw bad_optional_access();
  }
  /// \group value
  /// \synopsis constexpr const T &value() const;
  constexpr const T &value() const & {
    if (has_value())
      return this->value_;
    throw bad_optional_access();
  }
  /// \exclude
  constexpr T &&value() && {
    if (has_value())
      return std::move(this->value_);
    throw bad_optional_access();
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \exclude
  constexpr const T &&value() const && {
    if (has_value())
      return std::move(this->value_);
    throw bad_optional_access();
  }
#endif

  /// \returns the stored value if there is one, otherwise returns `u`
  /// \group value_or
  template <typename U> constexpr T value_or(U&& u) const & {
    static_assert(std::is_copy_constructible<T>::value &&
                      std::is_convertible<U&&, T>::value,
                  "T must be copy constructible and convertible from U");
    return has_value() ? **this : static_cast<T>(std::forward<U>(u));
  }

  /// \group value_or
  template <typename U> constexpr T value_or(U&& u) && {
    static_assert(std::is_move_constructible<T>::value &&
                      std::is_convertible<U&&, T>::value,
                  "T must be move constructible and convertible from U");
    return has_value() ? **this : static_cast<T>(std::forward<U>(u));
  }

  /// Destroys the stored value if one exists, making the optional empty
  void reset() noexcept {
    if (has_value()) {
      this->value_.~T();
      this->has_value_ = false;
    }
  }
}; // namespace tao

/// \group relop
/// \brief Compares two optional objects
/// \details If both optionals contain a value, they are compared with `T`s
/// relational operators. Otherwise `lhs` and `rhs` are equal only if they are
/// both empty, and `lhs` is less than `rhs` only if `rhs` is empty and `lhs`
/// is not.
template <typename T, typename U>
inline constexpr bool operator==(const optional<T>& lhs,
                                 const optional<U>& rhs) {
  return lhs.has_value() == rhs.has_value() &&
         (!lhs.has_value() || *lhs == *rhs);
}
/// \group relop
template <typename T, typename U>
inline constexpr bool operator!=(const optional<T>& lhs,
                                 const optional<U>& rhs) {
  return lhs.has_value() != rhs.has_value() ||
         (lhs.has_value() && *lhs != *rhs);
}
/// \group relop
template <typename T, typename U>
inline constexpr bool operator<(const optional<T>& lhs,
                                const optional<U>& rhs) {
  return rhs.has_value() && (!lhs.has_value() || *lhs < *rhs);
}
/// \group relop
template <typename T, typename U>
inline constexpr bool operator>(const optional<T>& lhs,
                                const optional<U>& rhs) {
  return lhs.has_value() && (!rhs.has_value() || *lhs > *rhs);
}
/// \group relop
template <typename T, typename U>
inline constexpr bool operator<=(const optional<T>& lhs,
                                 const optional<U>& rhs) {
  return !lhs.has_value() || (rhs.has_value() && *lhs <= *rhs);
}
/// \group relop
template <typename T, typename U>
inline constexpr bool operator>=(const optional<T>& lhs,
                                 const optional<U>& rhs) {
  return !rhs.has_value() || (lhs.has_value() && *lhs >= *rhs);
}

/// \group relop_nullopt
/// \brief Compares an optional to a `nullopt`
/// \details Equivalent to comparing the optional to an empty optional
template <typename T>
inline constexpr bool operator==(const optional<T>& lhs, nullopt_t) noexcept {
  return !lhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator==(nullopt_t, const optional<T>& rhs) noexcept {
  return !rhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator!=(const optional<T>& lhs, nullopt_t) noexcept {
  return lhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator!=(nullopt_t, const optional<T>& rhs) noexcept {
  return rhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator<(const optional<T>& , nullopt_t) noexcept {
  return false;
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator<(nullopt_t, const optional<T>& rhs) noexcept {
  return rhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator<=(const optional<T>& lhs, nullopt_t) noexcept {
  return !lhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator<=(nullopt_t, const optional<T>& ) noexcept {
  return true;
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator>(const optional<T>& lhs, nullopt_t) noexcept {
  return lhs.has_value();
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator>(nullopt_t, const optional<T>& ) noexcept {
  return false;
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator>=(const optional<T>& , nullopt_t) noexcept {
  return true;
}
/// \group relop_nullopt
template <typename T>
inline constexpr bool operator>=(nullopt_t, const optional<T>& rhs) noexcept {
  return !rhs.has_value();
}

/// \group relop_t
/// \brief Compares the optional with a value.
/// \details If the optional has a value, it is compared with the other value
/// using `T`s relational operators. Otherwise, the optional is considered
/// less than the value.
template <typename T, typename U>
inline constexpr bool operator==(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs == rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator==(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs == *rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator!=(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs != rhs : true;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator!=(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs != *rhs : true;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator<(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs < rhs : true;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator<(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs < *rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator<=(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs <= rhs : true;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator<=(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs <= *rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator>(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs > rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator>(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs > *rhs : true;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator>=(const optional<T>& lhs, const U &rhs) {
  return lhs.has_value() ? *lhs >= rhs : false;
}
/// \group relop_t
template <typename T, typename U>
inline constexpr bool operator>=(const U &lhs, const optional<T>& rhs) {
  return rhs.has_value() ? lhs >= *rhs : true;
}

/// \synopsis template <typename T>\nvoid swap(optional<T>& lhs, optional<T>& rhs);
template <typename T,
          detail::enable_if_t<std::is_move_constructible<T>::value> * = nullptr,
          detail::enable_if_t<detail::is_swappable<T>::value> * = nullptr>
void swap(optional<T>& lhs,
          optional<T>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  return lhs.swap(rhs);
}

namespace detail {
struct i_am_secret {};
} // namespace detail

template <typename T = detail::i_am_secret, typename U,
          typename Ret =
              detail::conditional_t<std::is_same<T, detail::i_am_secret>::value,
                                    detail::decay_t<U>, T>>
inline constexpr optional<Ret> make_optional(U&& v) {
  return optional<Ret>(std::forward<U>(v));
}

template <typename T, typename... Args>
inline constexpr optional<T> make_optional(Args &&... args) {
  return optional<T>(in_place, std::forward<Args>(args)...);
}
template <typename T, typename U, typename... Args>
inline constexpr optional<T> make_optional(std::initializer_list<U> il,
                                           Args &&... args) {
  return optional<T>(in_place, il, std::forward<Args>(args)...);
}

#if __cplusplus >= 201703L
template <typename T> optional(T)->optional<T>;
#endif

/// \exclude
namespace detail {
#ifdef TAO_OPTIONAL_CXX14
template <typename Opt, typename F,
          typename Ret = decltype(detail::invoke(std::declval<F>(),
                                              *std::declval<Opt>())),
          detail::enable_if_t<!std::is_void<Ret>::value> * = nullptr>
constexpr auto optional_map_impl(Opt &&opt, F &&f) {
  return opt.has_value()
             ? detail::invoke(std::forward<F>(f), *std::forward<Opt>(opt))
             : optional<Ret>(nullopt);
}

template <typename Opt, typename F,
          typename Ret = decltype(detail::invoke(std::declval<F>(),
                                              *std::declval<Opt>())),
          detail::enable_if_t<std::is_void<Ret>::value> * = nullptr>
auto optional_map_impl(Opt &&opt, F &&f) {
  if (opt.has_value()) {
    detail::invoke(std::forward<F>(f), *std::forward<Opt>(opt));
    return make_optional(monostate{});
  }

  return optional<monostate>(nullopt);
}
#else
template <typename Opt, typename F,
          typename Ret = decltype(detail::invoke(std::declval<F>(),
                                              *std::declval<Opt>())),
          detail::enable_if_t<!std::is_void<Ret>::value> * = nullptr>

constexpr auto optional_map_impl(Opt &&opt, F &&f) -> optional<Ret> {
  return opt.has_value()
             ? detail::invoke(std::forward<F>(f), *std::forward<Opt>(opt))
             : optional<Ret>(nullopt);
}

template <typename Opt, typename F,
          typename Ret = decltype(detail::invoke(std::declval<F>(),
                                              *std::declval<Opt>())),
          detail::enable_if_t<std::is_void<Ret>::value> * = nullptr>

auto optional_map_impl(Opt &&opt, F &&f) -> optional<monostate> {
  if (opt.has_value()) {
    detail::invoke(std::forward<F>(f), *std::forward<Opt>(opt));
    return monostate{};
  }

  return nullopt;
}
#endif
} // namespace detail

/// Specialization for when `T` is a reference. `optional<T&>` acts similarly
/// to a `T*`, but provides more operations and shows intent more clearly.
///
/// *Examples*:
///
/// ```
/// int i = 42;
/// tao::optional<int&> o = i;
/// *o == 42; //true
/// i = 12;
/// *o = 12; //true
/// &*o == &i; //true
/// ```
///
/// Assignment has rebind semantics rather than assign-through semantics:
///
/// ```
/// int j = 8;
/// o = j;
///
/// &*o == &j; //true
/// ```
template <typename T> class optional<T &> {
public:
// The different versions for C++14 and 11 are needed because deduced return
// types are not SFINAE-safe. This provides better support for things like
// generic lambdas. C.f.
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0826r0.html
#if defined(TAO_OPTIONAL_CXX14) && !defined(TAO_OPTIONAL_GCC49) &&               \
    !defined(TAO_OPTIONAL_GCC54) && !defined(TAO_OPTIONAL_GCC55)
  /// \group and_then
  /// Carries out some operation which returns an optional on the stored
  /// object if there is one. \requires `std::invoke(std::forward<F>(f),
  /// value())` returns a `std::optional<U>` for some `U`. \returns Let `U` be
  /// the result of `std::invoke(std::forward<F>(f), value())`. Returns a
  /// `std::optional<U>`. The return value is empty if `*this` is empty,
  /// otherwise the return value of `std::invoke(std::forward<F>(f), value())`
  /// is returned.
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &;
  template <typename F> constexpr auto and_then(F &&f) & {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &&;
  template <typename F> constexpr auto and_then(F &&f) && {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &;
  template <typename F> constexpr auto and_then(F &&f) const & {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &&;
  template <typename F> constexpr auto and_then(F &&f) const && {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }
#endif
#else
  /// \group and_then
  /// Carries out some operation which returns an optional on the stored
  /// object if there is one. \requires `std::invoke(std::forward<F>(f),
  /// value())` returns a `std::optional<U>` for some `U`. \returns Let `U` be
  /// the result of `std::invoke(std::forward<F>(f), value())`. Returns a
  /// `std::optional<U>`. The return value is empty if `*this` is empty,
  /// otherwise the return value of `std::invoke(std::forward<F>(f), value())`
  /// is returned.
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &;
  template <typename F>
  constexpr detail::invoke_result_t<F, T &> and_then(F &&f) & {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) &&;
  template <typename F>
  constexpr detail::invoke_result_t<F, T &> and_then(F &&f) && {
    using result = detail::invoke_result_t<F, T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &;
  template <typename F>
  constexpr detail::invoke_result_t<F, const T &> and_then(F &&f) const & {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group and_then
  /// \synopsis template <typename F>\nconstexpr auto and_then(F &&f) const &&;
  template <typename F>
  constexpr detail::invoke_result_t<F, const T &> and_then(F &&f) const && {
    using result = detail::invoke_result_t<F, const T &>;
    static_assert(detail::is_optional<result>::value,
                  "F must return an optional");

    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : result(nullopt);
  }
#endif
#endif

#if defined(TAO_OPTIONAL_CXX14) && !defined(TAO_OPTIONAL_GCC49) &&               \
    !defined(TAO_OPTIONAL_GCC54) && !defined(TAO_OPTIONAL_GCC55)
  /// \brief Carries out some operation on the stored object if there is one.
  /// \returns Let `U` be the result of `std::invoke(std::forward<F>(f),
  /// value())`. Returns a `std::optional<U>`. The return value is empty if
  /// `*this` is empty, otherwise an `optional<U>` is constructed from the
  /// return value of `std::invoke(std::forward<F>(f), value())` and is
  /// returned.
  ///
  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) &;
  template <typename F> constexpr auto map(F &&f) & {
    return detail::optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) &&;
  template <typename F> constexpr auto map(F &&f) && {
    return detail::optional_map_impl(std::move(*this), std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) const&;
  template <typename F> constexpr auto map(F &&f) const & {
    return detail::optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> constexpr auto map(F &&f) const&&;
  template <typename F> constexpr auto map(F &&f) const && {
    return detail::optional_map_impl(std::move(*this), std::forward<F>(f));
  }
#else
  /// \brief Carries out some operation on the stored object if there is one.
  /// \returns Let `U` be the result of `std::invoke(std::forward<F>(f),
  /// value())`. Returns a `std::optional<U>`. The return value is empty if
  /// `*this` is empty, otherwise an `optional<U>` is constructed from the
  /// return value of `std::invoke(std::forward<F>(f), value())` and is
  /// returned.
  ///
  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) &;
  template <typename F>
  constexpr decltype(detail::optional_map_impl(std::declval<optional &>(),
                                                     std::declval<F &&>()))
  map(F &&f) & {
    return detail::optional_map_impl(*this, std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) &&;
  template <typename F>
  constexpr decltype(detail::optional_map_impl(std::declval<optional &&>(),
                                                     std::declval<F &&>()))
  map(F &&f) && {
    return detail::optional_map_impl(std::move(*this), std::forward<F>(f));
  }

  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) const&;
  template <typename F>
  constexpr decltype(detail::optional_map_impl(std::declval<const optional &>(),
                                      std::declval<F &&>()))
  map(F &&f) const & {
    return detail::optional_map_impl(*this, std::forward<F>(f));
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map
  /// \synopsis template <typename F> auto map(F &&f) const&&;
  template <typename F>
  constexpr decltype(detail::optional_map_impl(std::declval<const optional &&>(),
                                      std::declval<F &&>()))
  map(F &&f) const && {
    return detail::optional_map_impl(std::move(*this), std::forward<F>(f));
  }
#endif
#endif

  /// \brief Calls `f` if the optional is empty
  /// \requires `std::invoke_result_t<F>` must be void or convertible to
  /// `optional<T>`. \effects If `*this` has a value, returns `*this`.
  /// Otherwise, if `f` returns `void`, calls `std::forward<F>(f)` and returns
  /// `std::nullopt`. Otherwise, returns `std::forward<F>(f)()`.
  ///
  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) &;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) & {
    if (has_value())
      return *this;

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) & {
    return has_value() ? *this : std::forward<F>(f)();
  }

  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) &&;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) && {
    if (has_value())
      return std::move(*this);

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) && {
    return has_value() ? std::move(*this) : std::forward<F>(f)();
  }

  /// \group or_else
  /// \synopsis template <typename F> optional<T> or_else (F &&f) const &;
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const & {
    if (has_value())
      return *this;

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> constexpr or_else(F &&f) const & {
    return has_value() ? *this : std::forward<F>(f)();
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \exclude
  template <typename F, detail::enable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const && {
    if (has_value())
      return std::move(*this);

    std::forward<F>(f)();
    return nullopt;
  }

  /// \exclude
  template <typename F, detail::disable_if_ret_void<F> * = nullptr>
  optional<T> or_else(F &&f) const && {
    return has_value() ? std::move(*this) : std::forward<F>(f)();
  }
#endif

  /// \brief Maps the stored value with `f` if there is one, otherwise returns
  /// `u`.
  ///
  /// \details If there is a value stored, then `f` is called with `**this`
  /// and the value is returned. Otherwise `u` is returned.
  ///
  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u);
  }

  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u);
  }

  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) const & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map_or
  template <typename F, typename U> U map_or(F &&f, U&& u) const && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u);
  }
#endif

  /// \brief Maps the stored value with `f` if there is one, otherwise calls
  /// `u` and returns the result.
  ///
  /// \details If there is a value stored, then `f` is
  /// called with `**this` and the value is returned. Otherwise
  /// `std::forward<U>(u)()` is returned.
  ///
  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u) &;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u)();
  }

  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// &&;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u)();
  }

  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// const &;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) const & {
    return has_value() ? detail::invoke(std::forward<F>(f), **this)
                       : std::forward<U>(u)();
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group map_or_else
  /// \synopsis template <typename F, typename U>\nauto map_or_else(F &&f, U&& u)
  /// const &&;
  template <typename F, typename U>
  detail::invoke_result_t<U> map_or_else(F &&f, U&& u) const && {
    return has_value() ? detail::invoke(std::forward<F>(f), std::move(**this))
                       : std::forward<U>(u)();
  }
#endif

  /// \returns `u` if `*this` has a value, otherwise an empty optional.
  template <typename U>
  constexpr optional<typename std::decay<U>::type> conjunction(U&& u) const {
    using result = optional<detail::decay_t<U>>;
    return has_value() ? result{u} : result{nullopt};
  }

  /// \returns `rhs` if `*this` is empty, otherwise the current value.
  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) & {
    return has_value() ? *this : rhs;
  }

  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) const & {
    return has_value() ? *this : rhs;
  }

  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) && {
    return has_value() ? std::move(*this) : rhs;
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group disjunction
  constexpr optional disjunction(const optional &rhs) const && {
    return has_value() ? std::move(*this) : rhs;
  }
#endif

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) & {
    return has_value() ? *this : std::move(rhs);
  }

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) const & {
    return has_value() ? *this : std::move(rhs);
  }

  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) && {
    return has_value() ? std::move(*this) : std::move(rhs);
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group disjunction
  constexpr optional disjunction(optional &&rhs) const && {
    return has_value() ? std::move(*this) : std::move(rhs);
  }
#endif

  /// Takes the value out of the optional, leaving it empty
  /// \group take
  optional take() & {
    optional ret = *this;
    reset();
    return ret;
  }

  /// \group take
  optional take() const & {
    optional ret = *this;
    reset();
    return ret;
  }

  /// \group take
  optional take() && {
    optional ret = std::move(*this);
    reset();
    return ret;
  }

#ifndef TAO_OPTIONAL_NO_CONSTRR
  /// \group take
  optional take() const && {
    optional ret = std::move(*this);
    reset();
    return ret;
  }
#endif

  using value_type = T &;

  /// Constructs an optional that does not contain a value.
  /// \group ctor_empty
  constexpr optional() noexcept : value_(nullptr) {}

  /// \group ctor_empty
  constexpr optional(nullopt_t) noexcept : value_(nullptr) {}

  /// Copy constructor
  ///
  /// If `rhs` contains a value, the stored value is direct-initialized with
  /// it. Otherwise, the constructed optional is empty.
  constexpr optional(const optional &rhs) noexcept = default;

  /// Move constructor
  ///
  /// If `rhs` contains a value, the stored value is direct-initialized with
  /// it. Otherwise, the constructed optional is empty.
  constexpr optional(optional &&rhs) = default;

  /// Constructs the stored value with `u`.
  /// \synopsis template <typename U=T> constexpr optional(U&& u);
  template <typename U = T,
            detail::enable_if_t<!detail::is_optional<detail::decay_t<U>>::value>
                * = nullptr>
  constexpr optional(U&& u) : value_(std::addressof(u)) {
    static_assert(std::is_lvalue_reference<U>::value, "U must be an lvalue");
  }

  /// \exclude
  template <typename U>
  constexpr explicit optional(const optional<U>& rhs) : optional(*rhs) {}

  /// No-op
  ~optional() = default;

  /// Assignment to empty.
  ///
  /// Destroys the current value if there is one.
  optional &operator=(nullopt_t) noexcept {
    value_ = nullptr;
    return *this;
  }

  /// Copy assignment.
  ///
  /// Rebinds this optional to the referee of `rhs` if there is one. Otherwise
  /// resets the stored value in `*this`.
  optional &operator=(const optional &rhs) = default;

  /// Rebinds this optional to `u`.
  ///
  /// \requires `U` must be an lvalue reference.
  /// \synopsis optional &operator=(U&& u);
  template <typename U = T,
            detail::enable_if_t<!detail::is_optional<detail::decay_t<U>>::value>
                * = nullptr>
  optional &operator=(U&& u) {
    static_assert(std::is_lvalue_reference<U>::value, "U must be an lvalue");
    value_ = std::addressof(u);
    return *this;
  }

  /// Converting copy assignment operator.
  ///
  /// Rebinds this optional to the referee of `rhs` if there is one. Otherwise
  /// resets the stored value in `*this`.
  template <typename U> optional &operator=(const optional<U>& rhs) {
    value_ = std::addressof(rhs.value());
    return *this;
  }

  /// Constructs the value in-place, destroying the current one if there is
  /// one.
  ///
  /// \group emplace
  template <typename... Args> T &emplace(Args &&... args) noexcept {
    static_assert(std::is_constructible<T, Args &&...>::value,
                  "T must be constructible with Args");

    *this = nullopt;
    this->construct(std::forward<Args>(args)...);
  }

  /// Swaps this optional with the other.
  ///
  /// If neither optionals have a value, nothing happens.
  /// If both have a value, the values are swapped.
  /// If one has a value, it is moved to the other and the movee is left
  /// valueless.
  void swap(optional &rhs) noexcept { std::swap(value_, rhs.value_); }

  /// \returns a pointer to the stored value
  /// \requires a value is stored
  /// \group pointer
  /// \synopsis constexpr const T *operator->() const;
  constexpr const T *operator->() const { return value_; }

  /// \group pointer
  /// \synopsis constexpr T *operator->();
  constexpr T *operator->() { return value_; }

  /// \returns the stored value
  /// \requires a value is stored
  /// \group deref
  /// \synopsis constexpr T &operator*();
  constexpr T &operator*() { return *value_; }

  /// \group deref
  /// \synopsis constexpr const T &operator*() const;
  constexpr const T &operator*() const { return *value_; }

  /// \returns whether or not the optional has a value
  /// \group has_value
  constexpr bool has_value() const noexcept { return value_ != nullptr; }

  /// \group has_value
  constexpr explicit operator bool() const noexcept {
    return value_ != nullptr;
  }

  /// \returns the contained value if there is one, otherwise throws
  /// [bad_optional_access]
  /// \group value
  /// synopsis constexpr T &value();
  constexpr T &value() {
    if (has_value())
      return *value_;
    throw bad_optional_access();
  }
  /// \group value
  /// \synopsis constexpr const T &value() const;
  constexpr const T &value() const {
    if (has_value())
      return *value_;
    throw bad_optional_access();
  }

  /// \returns the stored value if there is one, otherwise returns `u`
  /// \group value_or
  template <typename U> constexpr T value_or(U&& u) const & {
    static_assert(std::is_copy_constructible<T>::value &&
                      std::is_convertible<U&& , T>::value,
                  "T must be copy constructible and convertible from U");
    return has_value() ? **this : static_cast<T>(std::forward<U>(u));
  }

  /// \group value_or
  template <typename U> constexpr T value_or(U&& u) && {
    static_assert(std::is_move_constructible<T>::value &&
                      std::is_convertible<U&& , T>::value,
                  "T must be move constructible and convertible from U");
    return has_value() ? **this : static_cast<T>(std::forward<U>(u));
  }

  /// Destroys the stored value if one exists, making the optional empty
  void reset() noexcept { value_ = nullptr; }

private:
  T *value_;
}; // namespace tao



} // namespace tao

namespace std {
// TODO SFINAE
template <typename T> struct hash<tao::optional<T>> {
  ::std::size_t operator()(const tao::optional<T>& o) const {
    if (!o.has_value())
      return 0;

    return std::hash<tao::detail::remove_const_t<T>>()(*o);
  }
};
} // namespace std

#endif // TAO_RESULT_RESULT_HPP_