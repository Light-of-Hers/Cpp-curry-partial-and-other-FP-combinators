#ifndef __CRZ_FP_HH__
#define __CRZ_FP_HH__

#include <type_traits>
#include <functional>
#include <tuple>

namespace crz {

namespace __detail {

template<typename R, typename ...As>
struct __function_traits_base {
    using function_type = std::function<R(As...)>;

    using result_type = R;

    using argument_types = std::tuple<As...>;
};

template<typename F>
struct __function_traits;
template<typename F>
struct __function_traits<std::reference_wrapper<F>> : public __function_traits<F> {};
template<typename R, typename ...As>
struct __function_traits<R(*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...) const> : public __function_traits_base<R, As...> {};
template<typename F>
struct __function_traits : public __function_traits<decltype(&F::operator())> {};

}

namespace fp {

template<typename F>
struct function_traits : public __detail::__function_traits<std::decay_t<F>> {};

}

namespace __detail {

template<typename T>
auto __copy_or_move(const T &t) -> T {
    if constexpr (std::is_copy_constructible_v<T>) {
        return t;
    } else {
        return std::move(const_cast<T &>(t));
    }
}


template<typename F, typename T1, typename T2>
class __curry_cacher;
template<typename F, typename TA, typename A, typename ...As>
class __curry_cacher<F, TA, std::tuple<A, As...>> {
    F f;
    TA cached_args;
public:
    __curry_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(A arg) {
        auto new_cached_args = std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<A>(std::forward<A>(arg)));
        return __curry_cacher<F,
                decltype(new_cached_args),
                std::tuple<As...>>(__copy_or_move(f), std::move(new_cached_args));
    }
};
template<typename F, typename TA, typename A>
class __curry_cacher<F, TA, std::tuple<A>> {
    F f;
    TA cached_args;
public:
    __curry_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(A arg) {
        return std::apply(f, std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<A>(std::forward<A>(arg))));
    }
};


template<typename F, typename T1, typename T2>
class __partial_cacher;
template<typename F, typename TA, typename ...As>
class __partial_cacher<F, TA, std::tuple<As...>> {
    F f;
    TA cached_args;
public:
    __partial_cacher(F f, TA args) : f(std::move(f)), cached_args(std::move(args)) {}
    auto operator()(As... args) {
        return std::apply(f, std::tuple_cat(
                __copy_or_move(cached_args),
                std::tuple<As...>(std::forward<As>(args)...)));
    }
};


template<std::size_t I, typename T, typename = void>
struct __tuple_drop_n;
template<std::size_t I, typename T>
using __tuple_drop_n_t = typename __tuple_drop_n<I, T>::type;
template<typename ...Ts>
struct __tuple_drop_n<0, std::tuple<Ts...>> {
    using type = std::tuple<Ts...>;
};
template<std::size_t I, typename T, typename ...Ts>
struct __tuple_drop_n<I, std::tuple<T, Ts...>, std::enable_if_t<(I > 0)>> {
    using type = __tuple_drop_n_t<I - 1, std::tuple<Ts...>>;
};

}

namespace fp {

template<typename F>
auto curry(F f) {
    using arg_types = typename function_traits<F>::argument_types;
    if constexpr (std::tuple_size_v<arg_types> < 2) {
        return f;
    } else {
        return __detail::__curry_cacher<F, std::tuple<>, arg_types>
                (std::move(f), std::tuple<>());
    }
}
template<typename F, typename ...As>
auto partial(F f, As &&...args) {
    using arg_types = typename function_traits<F>::argument_types;
    static_assert(sizeof...(As) <= std::tuple_size_v<arg_types>, "Too many arguments");
    if constexpr (sizeof...(As) == 0) {
        return f;
    } else if constexpr (sizeof...(As) == std::tuple_size_v<arg_types>) {
        return f(std::forward<As>(args)...);
    } else {
        return __detail::__partial_cacher<F, std::tuple<As...>, __detail::__tuple_drop_n_t<sizeof...(As), arg_types>>
                (std::move(f), std::tuple<As...>(std::forward<As>(args)...));
    }
}

template<typename F>
auto pipe(F f) {
    return f;
}
template<typename F, typename ...Fs>
auto pipe(F f, Fs ...fs) {
    return [=, f = std::move(f)](auto &&...args) {
        return pipe(fs...)(f(std::forward<decltype(args)>(args)...));
    };
}

template<typename F>
auto compose(F f) {
    return f;
}
template<typename F, typename ...Fs>
auto compose(F f, Fs ...fs) {
    return [=, f = std::move(f)](auto &&...args) {
        return f(compose(fs...)(std::forward<decltype(args)>(args)...));
    };
}

template<typename F1, typename F2, typename J>
auto fork_join(F1 f1, F2 f2, J join) {
    return [f1 = std::move(f1), f2 = std::move(f2), join = std::move(join)](auto &&...args) {
        return join(f1(std::forward<decltype(args)>(args)...),
                    f2(std::forward<decltype(args)>(args)...));
    };
}

template<typename T>
auto identity(T &&t) {
    return std::forward<T>(t);
}

template<typename T>
auto seq(T &&t) {
    return std::forward<T>(t);
}
template<typename T, typename F, typename ...Fs>
auto seq(T &&t, F f, Fs ...fs) {
    f(std::forward<T>(t));
    return seq(std::forward<T>(t), fs...);
}

}
}

#endif // __CRZ_FP_HH__
