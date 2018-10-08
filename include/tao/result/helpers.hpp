//! \file tao/result/helpers.hpp
// Tao.Result
//
// Copyright Fernando Pelliccioni 2016-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


#ifndef TAO_RESULT_HELPERS_HPP_
#define TAO_RESULT_HELPERS_HPP_

#define TAO_OPTIONAL_VERSION_MAJOR 0
#define TAO_OPTIONAL_VERSION_MINOR 2

#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#if (defined(_MSC_VER) && _MSC_VER == 1900)
#define TAO_OPTIONAL_MSVC2015
#endif

#if (defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ <= 9 &&              \
     !defined(__clang__))
#define TAO_OPTIONAL_GCC49
#endif

#if (defined(__GNUC__) && __GNUC__ == 5 && __GNUC_MINOR__ <= 4 &&              \
     !defined(__clang__))
#define TAO_OPTIONAL_GCC54
#endif

#if (defined(__GNUC__) && __GNUC__ == 5 && __GNUC_MINOR__ <= 5 &&              \
     !defined(__clang__))
#define TAO_OPTIONAL_GCC55
#endif

#if (defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ <= 9 &&              \
     !defined(__clang__))
// GCC < 5 doesn't support overloading on const&& for member functions
#define TAO_OPTIONAL_NO_CONSTRR

// GCC < 5 doesn't support some standard C++11 type traits
#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(T)                                     \
  std::has_trivial_copy_constructor<T>::value
#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_ASSIGNABLE(T) std::has_trivial_copy_assign<T>::value

// This one will be different for GCC 5.7 if it's ever supported
#define TAO_OPTIONAL_IS_TRIVIALLY_DESTRUCTIBLE(T) std::is_trivially_destructible<T>::value

// GCC 5 < v < 8 has a bug in is_trivially_copy_constructible which breaks std::vector
// for non-copyable types
#elif (defined(__GNUC__) && __GNUC__ < 8 && !defined(__clang__))
#ifndef TAO_GCC_LESS_8_TRIVIALLY_COPY_CONSTRUCTIBLE_MUTEX
#define TAO_GCC_LESS_8_TRIVIALLY_COPY_CONSTRUCTIBLE_MUTEX
namespace tao { namespace detail {

template <typename T>
struct is_trivially_copy_constructible : std::is_trivially_copy_constructible<T> {};

template <typename T, typename A>
#ifdef _GLIBCXX_VECTOR
struct is_trivially_copy_constructible<std::vector<T, A>> : std::is_trivially_copy_constructible<T> {};
#endif      
}} //namespace tao::detail
#endif

#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(T) tao::detail::is_trivially_copy_constructible<T>::value
#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_ASSIGNABLE(T) std::is_trivially_copy_assignable<T>::value
#define TAO_OPTIONAL_IS_TRIVIALLY_DESTRUCTIBLE(T) std::is_trivially_destructible<T>::value
#else
#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(T) std::is_trivially_copy_constructible<T>::value
#define TAO_OPTIONAL_IS_TRIVIALLY_COPY_ASSIGNABLE(T) std::is_trivially_copy_assignable<T>::value
#define TAO_OPTIONAL_IS_TRIVIALLY_DESTRUCTIBLE(T) std::is_trivially_destructible<T>::value
#endif

#if __cplusplus > 201103L
#define TAO_OPTIONAL_CXX14
#endif

/// \exclude
namespace detail {
#ifndef TAO_TRAITS_MUTEX
#define TAO_TRAITS_MUTEX
// C++14-style aliases for brevity
template <typename T> 
using remove_const_t = typename std::remove_const<T>::type;

template <typename T>
using remove_reference_t = typename std::remove_reference<T>::type;

template <typename T> 
using decay_t = typename std::decay<T>::type;

template <bool E, typename T = void>
using enable_if_t = typename std::enable_if<E, T>::type;

template <bool B, typename T, typename F>
using conditional_t = typename std::conditional<B, T, F>::type;

// std::conjunction from C++17
template <typename...> struct conjunction : std::true_type {};
template <typename B> struct conjunction<B> : B {};
template <typename B, typename... Bs>
struct conjunction<B, Bs...>
    : std::conditional<bool(B::value), conjunction<Bs...>, B>::type {};

#if defined(_LIBCPP_VERSION) && __cplusplus == 201103L
#define TAO_OPTIONAL_LIBCXX_MEM_FN_WORKAROUND
#endif

// In C++11 mode, there's an issue in libc++'s std::mem_fn
// which results in a hard-error when using it in a noexcept expression
// in some cases. This is a check to workaround the common failing case.
#ifdef TAO_OPTIONAL_LIBCXX_MEM_FN_WORKAROUND
template <typename T> struct is_pointer_to_non_const_member_func : std::false_type{};
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...)> : std::true_type{};
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...)&> : std::true_type{};
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...)&&> : std::true_type{};        
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...) volatile> : std::true_type{};
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...) volatile&> : std::true_type{};
template <typename T, typename Ret, typename... Args>
struct is_pointer_to_non_const_member_func<Ret (T::*) (Args...) volatile&&> : std::true_type{};        

template <typename T> struct is_const_or_const_ref : std::false_type{};
template <typename T> struct is_const_or_const_ref<T const&> : std::true_type{};
template <typename T> struct is_const_or_const_ref<T const> : std::true_type{};    
#endif

// std::invoke from C++17
// https://stackoverflow.com/questions/38288042/c11-14-invoke-workaround
template <typename Fn, typename... Args,
#ifdef TAO_OPTIONAL_LIBCXX_MEM_FN_WORKAROUND
          typename = enable_if_t<!(is_pointer_to_non_const_member_func<Fn>::value 
                                 && is_const_or_const_ref<Args...>::value)>, 
#endif
          typename = enable_if_t<std::is_member_pointer<decay_t<Fn>>::value>,
          int = 0>
constexpr auto invoke(Fn &&f, Args &&... args) noexcept(
    noexcept(std::mem_fn(f)(std::forward<Args>(args)...)))
    -> decltype(std::mem_fn(f)(std::forward<Args>(args)...)) {
  return std::mem_fn(f)(std::forward<Args>(args)...);
}

template <typename Fn, typename... Args,
          typename = enable_if_t<!std::is_member_pointer<decay_t<Fn>>::value>>
constexpr auto invoke(Fn &&f, Args &&... args) noexcept(
    noexcept(std::forward<Fn>(f)(std::forward<Args>(args)...)))
    -> decltype(std::forward<Fn>(f)(std::forward<Args>(args)...)) {
  return std::forward<Fn>(f)(std::forward<Args>(args)...);
}

// std::invoke_result from C++17
template <typename F, typename, typename... Us> struct invoke_result_impl;

template <typename F, typename... Us>
struct invoke_result_impl<
    F, decltype(detail::invoke(std::declval<F>(), std::declval<Us>()...), void()),
    Us...> {
  using type = decltype(detail::invoke(std::declval<F>(), std::declval<Us>()...));
};

template <typename F, typename... Us>
using invoke_result = invoke_result_impl<F, void, Us...>;

template <typename F, typename... Us>
using invoke_result_t = typename invoke_result<F, Us...>::type;
#endif

// std::void_t from C++17
template <typename...> struct voider { using type = void; };
template <typename... Ts> using void_t = typename voider<Ts...>::type;


#ifdef _MSC_VER
// TODO make a version which works with MSVC
template <typename T, typename U = T> struct is_swappable : std::true_type {};

template <typename T, typename U = T> struct is_nothrow_swappable : std::true_type {};
#else
// https://stackoverflow.com/questions/26744589/what-is-a-proper-way-to-implement-is-swappable-to-test-for-the-swappable-concept
namespace swap_adl_tests {
// if swap ADL finds this then it would call std::swap otherwise (same
// signature)
struct tag {};

template <typename T> tag swap(T &, T &);
template <typename T, std::size_t N> tag swap(T (&a)[N], T (&b)[N]);

// helper functions to test if an unqualified swap is possible, and if it
// becomes std::swap
template <typename, typename> std::false_type can_swap(...) noexcept(false);
template <typename T, typename U,
          typename = decltype(swap(std::declval<T &>(), std::declval<U &>()))>
std::true_type can_swap(int) noexcept(noexcept(swap(std::declval<T &>(),
                                                    std::declval<U &>())));

template <typename, typename> std::false_type uses_std(...);
template <typename T, typename U>
std::is_same<decltype(swap(std::declval<T &>(), std::declval<U &>())), tag>
uses_std(int);

template <typename T>
struct is_std_swap_noexcept
    : std::integral_constant<bool,
                             std::is_nothrow_move_constructible<T>::value &&
                                 std::is_nothrow_move_assignable<T>::value> {};

template <typename T, std::size_t N>
struct is_std_swap_noexcept<T[N]> : is_std_swap_noexcept<T> {};

template <typename T, typename U>
struct is_adl_swap_noexcept
    : std::integral_constant<bool, noexcept(can_swap<T, U>(0))> {};
} // namespace swap_adl_tests

template <typename T, typename U = T>
struct is_swappable
    : std::integral_constant<
          bool,
          decltype(detail::swap_adl_tests::can_swap<T, U>(0))::value &&
              (!decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value ||
               (std::is_move_assignable<T>::value &&
                std::is_move_constructible<T>::value))> {};

template <typename T, std::size_t N>
struct is_swappable<T[N], T[N]>
    : std::integral_constant<
          bool,
          decltype(detail::swap_adl_tests::can_swap<T[N], T[N]>(0))::value &&
              (!decltype(
                   detail::swap_adl_tests::uses_std<T[N], T[N]>(0))::value ||
               is_swappable<T, T>::value)> {};

template <typename T, typename U = T>
struct is_nothrow_swappable
    : std::integral_constant<
          bool,
          is_swappable<T, U>::value &&
              ((decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value
                    &&detail::swap_adl_tests::is_std_swap_noexcept<T>::value) ||
               (!decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value &&
                    detail::swap_adl_tests::is_adl_swap_noexcept<T,
                                                                 U>::value))> {
};
#endif

} // namespace detail

#endif // TAO_RESULT_HELPERS_HPP_