#ifndef __CRZ_META_HH__
#define __CRZ_META_HH__

#include <type_traits>
#include <functional>
#include <tuple>

namespace crz {

namespace detail {

template<typename R, typename ...As>
struct __function_traits_base {
    using function_type = std::function<R(As...)>;

    using result_type = R;

    using argument_types = std::tuple<As...>;
};

template<typename F>
struct __function_traits : public __function_traits<decltype(&F::operator())> {};
template<typename R, typename ...As>
struct __function_traits<R(*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...)> : public __function_traits_base<R, As...> {};
template<typename R, typename C, typename ...As>
struct __function_traits<R(C::*)(As...) const> : public __function_traits_base<R, As...> {};

}

namespace ft {

template<typename F>
struct function_traits : public detail::__function_traits<std::decay_t<F>> {};

}

namespace detail {

template<typename, typename, typename = void>
class __functor_copy_wrapper;

template<typename F, typename R, typename ...As>
class __functor_copy_wrapper<F, std::function<R(As...)>,
        std::enable_if_t<!std::is_copy_constructible_v<F>>> {
    using self = __functor_copy_wrapper;

    F f;
public:
    explicit __functor_copy_wrapper(F f) : f(std::move(f)) {}
    __functor_copy_wrapper(const self &rhs) : f(std::move(const_cast<self &>(rhs).f)) {}
    __functor_copy_wrapper(self &&rhs) noexcept : f(std::move(rhs.f)) {}

    auto operator()(As ...args) {
        return f(std::forward<As>(args)...);
    }
};

template<typename F, typename R, typename ...As>
class __functor_copy_wrapper<F, std::function<R(As...)>,
        std::enable_if_t<std::is_copy_constructible_v<F>>> {
    F f;
public:
    explicit __functor_copy_wrapper(F f) : f(std::move(f)) {}

    auto operator()(As ...args) {
        return f(std::forward<As>(args)...);
    }
};

template<typename R, typename A, typename ...As>
class __single_partialer {
    std::function<R(A, As...)> f;
    A arg;
public:
    __single_partialer(std::function<R(A, As...)> f, A arg)
            : f(std::move(f)), arg(std::forward<A>(arg)) {}
    auto operator()(As... args) {
        return f(std::forward<A>(arg), std::forward<As>(args)...);
    }
};

template<typename R, typename A = void>
auto __curry(std::function<R(A)> f) {
    return f;
}

template<typename R, typename A, typename ...As>
auto __curry(std::function<R(A, As...)> f) {
    return [f = std::move(f)](A arg) {
        auto partialer = __single_partialer<R, A, As...>(std::move(f), std::forward<A>(arg));
        __functor_copy_wrapper<decltype(partialer), std::function<R(As...)>>
                wrapper(std::move(partialer));
        std::function<R(As...)> rest = wrapper;
        return __curry(std::move(rest));
    };
}

template<typename R, typename ...Ps>
auto __partial(std::function<R(Ps...)> f) {
    return f;
}

template<typename R, typename P, typename ...Ps, typename A, typename ...As>
auto __partial(std::function<R(P, Ps...)> f, A &&arg, As &&...args) {
    auto partialer = __single_partialer<R, P, Ps...>(std::move(f), std::forward<A>(arg));
    __functor_copy_wrapper<decltype(partialer), std::function<R(Ps...)>>
            wrapper(std::move(partialer));
    std::function<R(Ps...)> rest = wrapper;
    return __partial(std::move(rest), std::forward<As>(args)...);
}

}

namespace ft {

template<typename F>
auto curry(F f) {
    using func_type = typename function_traits<F>::function_type;
    detail::__functor_copy_wrapper<F, func_type> wrapper(std::move(f));
    func_type functor = wrapper;
    return detail::__curry(std::move(functor));
}
template<typename F, typename ...As>
auto partial(F f, As &&...args) {
    using func_type = typename function_traits<F>::function_type;
    detail::__functor_copy_wrapper<F, func_type> wrapper(std::move(f));
    func_type functor = wrapper;
    return detail::__partial(std::move(functor), std::forward<As>(args)...);
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

#endif //__CRZ_META_HH__
