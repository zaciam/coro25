#pragma once
#ifndef PROMISE_H
#define PROMISE_H
/**
 * Enable this for CO2 coroutine support.
 */
#ifndef CO2
#define CO2 0
#endif


#if CO2
#include "co2/Co2.h"
#else
#ifdef _MSC_VER
#ifndef __cpp_lib_coroutine
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#define __builtin_coro_noop() nullptr  // NOLINT
#define __cpp_lib_coroutine 201902L  // NOLINT
#pragma GCC diagnostic pop
#endif
#endif
#include <coroutine>
#endif

#include <cassert>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <cstddef>
#include <type_traits>
#define PENDING 0
#define REJECTED 1
#define RESOLVED 2

namespace promise { namespace detail {

template<class Return> struct CallbackBase;

/**
 * \brief Represents the pending state of a promise.
 * \tparam Return The type of the value that the promise will return.
 */
template<class Return> struct PendingState {
  CallbackBase<Return>* callback;  ///< Pointer to the callback object.
  void* coroutine;  ///< Pointer to continue with.
};


/**
 * \brief Represents the state of a promise without using std::variant.
 * \tparam Return The type of the value that the promise will return.
 */
template<class Return> struct PromiseState {
  size_t index_;

  union U {
    PendingState<Return> pending;
    Return resolved;
    std::exception_ptr rejected;

    U() { }

    ~U() { }

    U(const U&) = delete;

    U(U&& /*unused*/) noexcept { }

    auto operator=(const U&) = delete;

    auto operator=(U&& /*unused*/) noexcept -> U& { return *this; }
  } u;

  /**
   * \brief Constructor with a pending state.
   * \param pendingState The pending state to initialize with.
   */
  explicit PromiseState(const PendingState<Return>& pendingState) : index_(PENDING) { u.pending = pendingState; }

  /**
   * \brief Default constructor.
   */
  PromiseState() : index_(PENDING) { new (&u.pending) PendingState<Return>(); }

  PromiseState(const PromiseState&) = delete;

  /**
   * \brief Move constructor.
   * \param other The other PromiseState to move from.
   */
  PromiseState(PromiseState&& other) noexcept : index_(other.index_) {
    if (other.index_ == PENDING) { u.pending = std::move(other.u.pending); }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe
    if (other.index_ == RESOLVED) { resolved(std::move(other.u.resolved)); }
    if (other.index_ == REJECTED) { u.rejected = std::move(other.u.rejected); }
  }

  /**
   * \brief Copy assignment operator.
   * \param other The other PromiseState to copy from.
   * \return A reference to this PromiseState.
   */
  auto operator=(const PromiseState& other) = delete;

  /**
   * \brief Move assignment operator.
   * \param other The other PromiseState to move from.
   * \return A reference to this PromiseState.
   */
  auto operator=(PromiseState&& other) noexcept -> PromiseState& {
    if (this != &other) {
      index_ = other.index_;
      if (other.index_ == PENDING) { u.pending = std::move(other.u.pending); }
      if (other.index_ == RESOLVED) { new (&u.resolved) Return(std::move(other.u.resolved)); }
      if (other.index_ == REJECTED) { u.rejected = std::move(other.u.rejected); }
    }
    return *this;
  }

  /**
   * \brief Destructor.
   */
  ~PromiseState() { clear(); }

  /**
   * \brief Clears the current state.
   */
  void clear() {
    if (index_ == RESOLVED) { u.resolved.~Return(); }
  }

  /**
   * \brief Gets the pending state.
   * \return The pending state.
   */
  auto pending() -> auto& { return u.pending; }

  /**
   * \brief Gets the rejected state.
   * \return The rejected state.
   */
  auto rejected() -> auto& { return u.rejected; }

  /**
   * \brief Gets the resolved state.
   * \return The resolved state.
   */
  auto resolved() -> auto& { return u.resolved; }

  /**
   * \brief Gets the index of the current state.
   * \return The index of the current state.
   */
  auto index() const -> size_t { return index_; }

  /**
   * \brief Sets the pending state.
   * \param pendingState The pending state to set.
   */
  template<class T> void pending(T&& pendingState) {
    clear();
    index_ = PENDING;
    u.pending = std::forward<T>(pendingState);
  }

  /**
   * \brief Sets the resolved state.
   * \param value The resolved state to set.
   */
  template<class T, std::enable_if_t<std::is_move_constructible_v<std::decay_t<T>>, bool> = true> void resolved_(T&& value, int /*unused*/) {
    clear();
    index_ = RESOLVED;
    new (&u.resolved) Return(std::forward<T>(value));
  }

  /**
   * \brief Sets the resolved state.
   * \param value The resolved state to set.
   */
  template<class T> void resolved_(const T& value, unsigned /*unused*/) {
    clear();
    index_ = RESOLVED;
    new (&u.resolved) Return(value);
  }

  /**
   * \brief Sets the resolved state.
   * \param value The resolved state to set.
   */
  template<class T> void resolved(T&& value) { resolved_(std::forward<T>(value), 0); }

  /**
   * \brief Sets the rejected state.
   * \param error The rejected state to set.
   */
  void resolved(std::exception_ptr error) {
    clear();
    index_ = REJECTED;
    u.rejected = error;
  }

  /**
   * \brief Checks if the state is pending.
   * \return Pointer to the pending state if it is pending, nullptr otherwise.
   */
  auto ifPending() -> decltype(auto) { return index_ == PENDING ? &u.pending : nullptr; }

  /**
   * \brief Checks if the state is rejected.
   * \return Pointer to the rejected state if it is rejected, nullptr otherwise.
   */
  auto ifRejected() -> decltype(auto) { return index_ == REJECTED ? &u.rejected : nullptr; }

  /**
   * \brief Checks if the state is rejected.
   * \return Pointer to the rejected state if it is rejected, nullptr otherwise.
   */
  auto ifRejected() const -> decltype(auto) { return index_ == REJECTED ? &u.rejected : nullptr; }

  /**
   * \brief Checks if the state is resolved.
   * \return Pointer to the resolved state if it is resolved, nullptr otherwise.
   */
  auto ifResolved() -> decltype(auto) { return index_ == RESOLVED ? &u.resolved : nullptr; }

  /**
   * \brief Checks if the state is resolved.
   * \return Pointer to the resolved state if it is resolved, nullptr otherwise.
   */
  auto ifResolved() const -> decltype(auto) { return index_ == RESOLVED ? &u.resolved : nullptr; }

  /**
   * \brief Checks if the state is pending.
   * \return True if the state is pending, false otherwise.
   */
  auto isPending() const -> bool { return index_ == PENDING; }
};

/**
 * \brief Specialization of PromiseState for void without using std::variant.
 */
template<> struct PromiseState<void> {
  size_t index_;

  union U {
    PendingState<void> pending {};
    std::exception_ptr rejected;

    U() { }

    ~U() { }

    U(const U&) = delete;

    U(U&& /*unused*/) noexcept { }

    auto operator=(const U&) = delete;

    auto operator=(U&& /*unused*/) noexcept -> U& { return *this; }
  } u;

  /**
   * \brief Constructor with a pending state.
   * \param pendingState The pending state to initialize with.
   */
  explicit PromiseState(const PendingState<void>& pendingState) : index_(PENDING) { u.pending = pendingState; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union writes are safe

  /**
   * \brief Default constructor.
   */
  PromiseState() : index_(PENDING) { new (&u.pending) PendingState<void>(); }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union writes are safe

  /**
   * \brief Move constructor.
   * \param other The other PromiseState to move from.
   */
  PromiseState(PromiseState&& other) noexcept : index_(other.index_) {
    if (other.index_ == PENDING) { u.pending = other.u.pending; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe
    if (other.index_ == REJECTED) { u.rejected = other.u.rejected; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe
  }

  PromiseState(const PromiseState&) = delete;
  auto operator=(const PromiseState&) = delete;

  /**
   * \brief Move assignment operator.
   * \param other The other PromiseState to move from.
   * \return A reference to this PromiseState.
   */
  auto operator=(PromiseState&& other) noexcept -> PromiseState& {
    if (this != &other) {
      index_ = other.index_;
      if (other.index_ == PENDING) { u.pending = other.u.pending; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe
      if (other.index_ == REJECTED) { u.rejected = other.u.rejected; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe
    }
    return *this;
  }

  /**
   * \brief Destructor.
   */
  ~PromiseState() = default;

  /**
   * \brief Gets the pending state.
   * \return The pending state.
   */
  auto pending() -> PendingState<void>& { return u.pending; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: caller must ensure type safety

  /**
   * \brief Gets the rejected state.
   * \return The rejected state.
   */
  auto rejected() -> std::exception_ptr& { return u.rejected; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: caller must ensure type safety

  /**
   * \brief Gets the index of the current state.
   * \return The index of the current state.
   */
  auto index() const -> size_t { return index_; }

  /**
   * \brief Sets the pending state.
   * \param pendingState The pending state to set.
   */
  template<class T> void pending(T&& pendingState) {
    index_ = PENDING;
    u.pending = std::forward<T>(pendingState);  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: writes are safe
  }

  /**
   * \brief Sets the resolved state.
   */
  void resolved() { index_ = RESOLVED; }

  /**
   * \brief Sets the rejected state.
   * \param error The rejected state to set.
   */
  void resolved(const std::exception_ptr& error) {
    index_ = REJECTED;
    u.rejected = error;  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: writes are safe
  }

  /**
   * \brief Checks if the state is pending.
   * \return Pointer to the pending state if it is pending, nullptr otherwise.
   */
  auto ifPending() -> PendingState<void>* { return index_ == PENDING ? &u.pending : nullptr; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe

  /**
   * \brief Checks if the state is rejected.
   * \return Pointer to the rejected state if it is rejected, nullptr otherwise.
   */
  auto ifRejected() const -> const std::exception_ptr* { return index_ == REJECTED ? &u.rejected : nullptr; }  // NOLINT(cppcoreguidelines-pro-type-union-access) justification: union is tagged with index_ so it is safe

  /**
   * \brief Checks if the state is pending.
   * \return True if the state is pending, false otherwise.
   */
  auto isPending() const -> bool { return index_ == PENDING; }
};


}  // namespace detail
}  // namespace promise

#undef PENDING
#undef REJECTED
#undef RESOLVED
#undef VARIANT

namespace promise {
template<class Return> struct Callback;

namespace detail {
template<class Return> struct PromiseImpl;

/**
 * \brief Base implementation of a promise.
 * \tparam Return The type of the value that the promise will return.
 */
template<class Return> struct PromiseImplBase {
  friend struct CallbackBase<Return>;
  friend struct PromiseImpl<Return>;
  using type = Return;
  detail::PromiseState<Return> state;

  /**
   * \brief Default constructor.
   */
  PromiseImplBase() = default;

private:
  explicit PromiseImplBase(CallbackBase<Return>* callback) : state(detail::PendingState<Return> {callback, nullptr}) { callback->link(this); }

  /**
   * \brief Move constructor.
   * \param other The other PromiseImplBase to move from.
   */
public:
  PromiseImplBase(PromiseImplBase<Return>&& other) noexcept : state(std::move(other.state)) {
    if (auto* state_ = state.ifPending()) {
      if (state_->callback != nullptr) { state_->callback->link(this); }
    }
  }

  PromiseImplBase(const PromiseImplBase<Return>& other) = delete;
  auto operator=(const PromiseImplBase<Return>& other) = delete;

  /**
   * \brief Destructor.
   */
  ~PromiseImplBase() {
    if (auto* state_ = state.ifPending()) {
      if (state_->callback != nullptr) { state_->callback->unlink(); }
    }
  }

  /**
   * \brief Move assignment operator.
   * \param other The other PromiseImplBase to move from.
   * \return A reference to this PromiseImplBase.
   */
  auto operator=(PromiseImplBase<Return>&& other) noexcept -> PromiseImplBase& {
    if (this != &other) {
      if (auto* state_ = state.ifPending()) {
        if (state_->callback != nullptr) {
          state_->callback->unlink();
          state_->callback = nullptr;
        }
      }
      state = std::move(other.state);
      if (auto* state_ = state.ifPending()) {
        if (state_->callback != nullptr) { state_->callback->link(this); }
      }
    }
    return *this;
  }
#if CO2
  /**
   * \brief Checks if the promise is ready.
   * \return True if the promise is not pending, false otherwise.
   */
  bool await_ready() const noexcept { return !state.isPending(); }

  /**
   * \brief Suspends the coroutine.
   * \param coroutine The coroutine to suspend.
   */
  void await_suspend(co2::coroutine<>& coroutine) { state.pending().coroutine = coroutine.detach(); }
#endif
private:
  void link(CallbackBase<Return>* callback) { state.pending().callback = static_cast<Callback<Return>*>(callback); }

  void unlink() {
    state.pending().callback = nullptr;
    try {
      throw std::runtime_error("Callback out of scope. Promise rejected.");
    } catch (...) {
      detail::PendingState<Return> pendingState = state.pending();
      state.resolved(std::current_exception());
      if (pendingState.callback != nullptr) { pendingState.callback->promise = nullptr; }
#if CO2
      if (pendingState.coroutine != nullptr) { co2::coroutine<>((co2::coroutine_handle)pendingState.coroutine).resume(); }
#else
      if (pendingState.coroutine != nullptr) { std::coroutine_handle<>::from_address(pendingState.coroutine).resume(); }
#endif
    }
  }
};

/**
 * \brief Implementation of a promise.
 * \tparam Return The type of the value that the promise will return.
 */
template<class Return> struct PromiseImpl : public detail::PromiseImplBase<Return> {
  using Base = detail::PromiseImplBase<Return>;
  using Base::state;

  /**
   * \brief Default constructor.
   */
  PromiseImpl() : Base() { }

  /**
   * \brief Constructor with a parameter.
   * \param callback The parameter to construct with.
   */
  explicit PromiseImpl(Callback<Return>* callback) : Base(callback) { }

  /**
   * \brief Move assignment operator.
   * \param other The parameter to assign from.
   * \return A reference to this PromiseImpl.
   */
  template<class T> auto operator=(T&& other) noexcept -> PromiseImpl& {
    Base::operator=(std::forward<T>(other));
    return *this;
  }

  /**
   * \brief Stream insertion operator.
   * \param outStream The output stream.
   * \param promise The promise to insert into the stream.
   * \return The output stream.
   */
  friend auto operator<<(std::ostream& outStream, const PromiseImpl& promise) -> std::ostream& {
    if (const auto* value = promise.state.ifResolved()) { return outStream << "Promise { " << *value << " }"; }
    if (const auto error = promise.state.ifRejected()) {
      try {
        std::rethrow_exception(*error);
      } catch (std::exception& e) { return outStream << "Promise { <rejected> Error: " << e.what() << " }"; } catch (...) {
        return outStream << "Promise { <rejected> }";
      }
    }
    return outStream << "Promise { <pending> }";
  }
#if CO2
  /**
   * \brief Resumes the coroutine and returns the result.
   * \return The result of the promise.
   */
  decltype(auto) await_resume() { return await_resume_<Return>(0); }

private:
  template<class Return_, std::enable_if_t<std::is_copy_constructible_v<Return_>, bool> = true> Return_ await_resume_(int) {
    if (auto value = state.ifResolved()) { return *value; }
    if (auto error = state.ifRejected()) { std::rethrow_exception(*error); }
    throw std::runtime_error("Promise is still in pending state. Cannot resume.");
  }

  template<class Return_> Return_&& await_resume_(long) {
    if (auto value = state.ifResolved()) { return std::move(*value); }
    if (auto error = state.ifRejected()) { std::rethrow_exception(*error); }
    throw std::runtime_error("Promise is still in pending state. Cannot resume.");
  }
#endif
};

/**
 * \brief Specialization of PromiseImpl for void.
 */
template<> struct PromiseImpl<void> : public detail::PromiseImplBase<void> {
  using Base = detail::PromiseImplBase<void>;

  /**
   * \brief Default constructor.
   */
  PromiseImpl() = default;

  /**
   * \brief Constructor with a parameter.
   * \param callback The parameter to construct with.
   */
  explicit PromiseImpl(CallbackBase<void>* callback) : Base(callback) { }

  /**
   * \brief Move assignment operator.
   * \param other The parameter to assign from.
   * \return A reference to this PromiseImpl.
   */
  template<class T> auto operator=(T&& other) noexcept -> PromiseImpl& {
    Base::operator=(std::forward<T>(other));
    return *this;
  }
#if CO2
  /**
   * \brief Resumes the coroutine.
   */
  void await_resume() { }
#endif
  /**
   * \brief Stream insertion operator.
   * \param outStream The output stream.
   * \param promise The promise to insert into the stream.
   * \return The output stream.
   */
  friend auto operator<<(std::ostream& outStream, const PromiseImpl& promise) -> std::ostream& {
    if (!promise.state.isPending()) { return outStream << "Promise { <resolved> }"; }
    if (const auto* error = promise.state.ifRejected()) {
      try {
        std::rethrow_exception(*error);
      } catch (std::exception& e) { return outStream << "Promise { <rejected> Error: " << e.what() << " }"; } catch (...) {
        return outStream << "Promise { <rejected> }";
      }
    }
    return outStream << "Promise { <pending> }";
  }
};

/**
 * \brief Base class for callbacks.
 * \tparam Return The type of the value that the callback will return.
 */
template<class Return> struct CallbackBase {
  friend struct detail::PromiseImplBase<Return>;
  friend struct Callback<Return>;
  PromiseImplBase<Return>* promise;

  /**
   * \brief Default constructor.
   */
  CallbackBase() : promise(nullptr) { }

  CallbackBase(const CallbackBase&) = delete;
  auto operator=(const CallbackBase&) = delete;

  /**
   * \brief Move constructor.
   * \param other The other CallbackBase to move from.
   */
  CallbackBase(CallbackBase&& other) noexcept : promise(other.promise) {
    if (promise != nullptr) { promise->link(this); }
    other.promise = nullptr;
  }

private:
  explicit CallbackBase(PromiseImplBase<Return>* promise_) : promise(promise_) {
    if (promise_ != nullptr) { promise_->link(this); }
  }

public:
  /**
   * \brief Destructor.
   */
  ~CallbackBase() {
    if (promise != nullptr) { promise->unlink(); }
  }

  /**
   * \brief Move assignment operator.
   * \param other The other CallbackBase to move from.
   * \return A reference to this CallbackBase.
   */
  auto operator=(CallbackBase&& other) noexcept -> CallbackBase& {
    if (this != &other) {
      if (promise != nullptr) { promise->state.pending().callback = nullptr; }
      promise = other.promise;
      if (promise != nullptr) { promise->link(this); }
      other.promise = nullptr;
    }
    return *this;
  }

  void store(void** user) { new (user) CallbackBase<Return>(std::move(*this)); }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundefined-reinterpret-cast"

  static auto load(void** user) -> Callback<Return>& { return *reinterpret_cast<Callback<Return>*>(user); }  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) justification: user is a void* pointer to a Callback object

#pragma GCC diagnostic pop

private:
  void link(PromiseImplBase<Return>* promise_) {
    if (promise != nullptr) { promise->state.pending().callback = nullptr; }
    assert(promise_ != nullptr);
    promise = promise_;
  }

  void unlink() { promise = nullptr; }
};
}  // namespace detail
}  // namespace promise

namespace promise {

/**
 * \brief A promise that can be used to retrieve a value asynchronously.
 *
 * A Promise represents a value that may be available now, or in the future, or never.
 * It allows you to write asynchronous code in a more synchronous fashion.
 *
 * The Promise can be in one of three states:
 * - Pending: The asynchronous operation has not yet completed.
 * - Resolved: The asynchronous operation has completed successfully.
 * - Rejected: The asynchronous operation has failed.
 *
 * \tparam Return The type of the value that the promise will return.
 *
 * \code
 * // usage example showing a simple sleep function using libuv
 * static auto mysleep(int timeout) -> Promise<void> {
 *   return Promise<void>([timeout](auto callback) {
 *     callback.store(&tim.data);
 *     uv_timer_start(&tim, [](uv_timer_t* hndl) { Callback<void>::load(&hndl->data)(); }, timeout, 0);
 *   });
 * }
 * \endcode
 *
 * \code
 * // usage example showing a simple coroutine using mysleep
 * auto exampleCoroutine() -> Promise<void> {
 *   std::cout << "Starting coroutine..." << std::endl;
 *   co_await mysleep(1000);
 *   std::cout << "Coroutine resumed after 1 second." << std::endl;
 * }
 * \endcode
 */
template<class Return> struct Promise : public detail::PromiseImpl<Return> {
  using Base = detail::PromiseImpl<Return>;
  using Base::state;
  using typename Base::type;

  /**
   * \brief Default constructor.
   */
  Promise() = default;

  /**
   * \brief Move constructor.
   * \param other The other Promise to move from.
   */
  Promise(Promise&& other) = default;

  /**
   * \brief Constructor with a callback.
   * \param callback The callback to associate with the promise.
   */
  explicit Promise(Callback<Return>* callback) : Base(callback) { }

  Promise(const Promise<Return>& other) = delete;
  auto operator=(const Promise<Return>& other) = delete;
  ~Promise() = default;

  /**
   * \brief Move assignment operator.
   * \param other The other Promise to move from.
   * \return A reference to this Promise.
   */
  auto operator=(Promise&& other) noexcept -> Promise& {
    Base::operator=(std::move(other));
    return *this;
  }

  /**
   * \brief Constructor with an executor.
   *
   * The executor is a function that takes a Callback as an argument and performs
   * the asynchronous operation. When the operation completes, the executor should
   * invoke the Callback with the result.
   *
   * \tparam Executor The type of the executor.
   * \param executor The executor to run.
   */
  template<class Executor, std::enable_if_t<!std::is_same_v<Promise<Return>&, Executor>, int> = 0> explicit Promise(Executor&& executor) {
    try {
      std::forward<Executor>(executor)(Callback<Return>(this));
    } catch (...) {
      if (auto* state_ = state.ifPending()) {
        detail::PendingState<Return> pendingState = *state_;
        state.resolved(std::current_exception());
        if (pendingState.callback != nullptr) { pendingState.callback->promise = nullptr; }
#if CO2
        if (pendingState.coroutine != nullptr) { co2::coroutine<>((co2::coroutine_handle)pendingState.coroutine).resume(); }
#else
        if (pendingState.coroutine != nullptr) { std::coroutine_handle<>::from_address(pendingState.coroutine).resume(); }
#endif
      }
    }
  }
};

/**
 * \brief A callback that can be used to fulfill a promise.
 *
 * The Callback is used to provide the result of an asynchronous operation to the Promise.
 * When the operation completes, the Callback is invoked with the result, which transitions
 * the Promise from the Pending state to either the Resolved or Rejected state.
 *
 * \tparam Return The type of the value that the callback will return.
 */
template<class Return> struct Callback : public detail::CallbackBase<Return> {
  friend struct detail::PromiseImplBase<Return>;
  using Base = detail::CallbackBase<Return>;
  using Base::promise;
  using type = Return;

  /**
   * \brief Default constructor.
   */
  Callback() = default;

  /**
   * \brief Constructor with a parameter.
   * \param promise The parameter to construct with.
   */
  explicit Callback(Promise<Return>* promise) : Base(promise) { }

  /**
   * \brief Move assignment operator with a parameter.
   * \param other The parameter to assign from.
   * \return A reference to this Callback.
   */
  template<class T> Callback& operator=(T&& other) noexcept {
    Base::operator=(std::forward<T>(other));
    return *this;
  }

  /**
   * \brief Invokes the callback with a value.
   *
   * This method is called when the asynchronous operation completes successfully.
   * It transitions the associated Promise to the Resolved state with the provided value.
   *
   * \tparam T The type of the value.
   * \param value The value to fulfill the promise with.
   */
  template<class T> void operator()(T&& value) {
    if (promise != nullptr) {
      void* coroutine = promise->state.pending().coroutine;
      promise->state.resolved(std::forward<T>(value));
      promise = nullptr;
#if CO2
      if (coroutine != nullptr) { co2::coroutine<>((co2::coroutine_handle)coroutine).resume(); }
#else
      if (coroutine != nullptr) { std::coroutine_handle<>::from_address(coroutine).resume(); }
#endif
    }
  }
};

/**
 * \brief Specialization of Callback for void.
 *
 * This specialization is used when the asynchronous operation does not return a value.
 */
template<> struct Callback<void> : public detail::CallbackBase<void> {
  friend struct detail::PromiseImplBase<void>;
  using Base = detail::CallbackBase<void>;
  using Base::promise;
  using type = void;

  /**
   * \brief Default constructor.
   */
  Callback() = default;
  Callback(Callback&& other) = default;
  Callback(const Callback& other) = delete;
  auto operator=(Callback&& other) -> Callback& = default;
  auto operator=(const Callback& other) = delete;
  ~Callback() = default;

  /**
   * \brief Constructor with a parameter.
   * \param promise_ The parameter to construct with.
   */
  explicit Callback(Promise<void>* promise_) : Base(promise_) { }

  /**
   * \brief Invokes the callback.
   * This method is called when the asynchronous operation completes successfully.
   * It transitions the associated Promise to the Resolved state.
   */
  void operator()() {
    if (promise != nullptr) {
      void* coroutine = promise->state.pending().coroutine;
      promise->state.resolved();
      promise = nullptr;
#if CO2
      if (coroutine != nullptr) { co2::coroutine<>((co2::coroutine_handle)coroutine).resume(); }
#else
      if (coroutine != nullptr) { std::coroutine_handle<>::from_address(coroutine).resume(); }
#endif
    }
  }
};

namespace detail {
#if !CO2
/**
 * \brief Removes rvalue reference from a type.
 * \tparam T The type to remove rvalue reference from.
 */
template<class T> struct remove_rvalue_reference : std::type_identity<T> { };

/**
 * \brief Specialization for rvalue references.
 * \tparam T The type to remove rvalue reference from.
 */
template<class T> struct remove_rvalue_reference<T&&> : std::type_identity<T&&> { };
/**
 * \brief Alias for remove_rvalue_reference.
 * \tparam T The type to remove rvalue reference from.
 */
template<class T> using remove_rvalue_reference_t = typename remove_rvalue_reference<T>::type;

/**
 * \brief Base class for awaiting a promise.
 * \tparam Promise The type of the promise.
 */
template<class Promise> struct AwaitBase {
  Promise promise;

public:
  /**
   * \brief Constructor with a promise.
   * \param promise_ The promise to await.
   */
  template<class T> explicit AwaitBase(T&& promise_) : promise(std::forward<T>(promise_)) { }

  AwaitBase(const AwaitBase&) = delete;
  AwaitBase(AwaitBase&&) = delete;
  auto operator=(AwaitBase&&) = delete;
  auto operator=(const AwaitBase&) = delete;
  ~AwaitBase() = default;

  /**
   * \brief Checks if the promise is ready.
   * \return True if the promise is not pending, false otherwise.
   */
  auto await_ready() const noexcept -> bool { return !promise.state.isPending(); }

  /**
   * \brief Suspends the coroutine.
   * \param callback The coroutine handle to suspend.
   */
  void await_suspend(std::coroutine_handle<> callback) { promise.state.pending().coroutine = callback.address(); }
};

/**
 * \brief Await a promise.
 * \tparam Promise The type of the promise.
 */
template<class Promise> struct Await : public AwaitBase<Promise> {
  using Base = AwaitBase<Promise>;
  using Base::promise;
  using Return = typename std::decay_t<Promise>::type;

  /**
   * \brief Constructor with a promise.
   * \param promise The promise to await.
   */
  explicit Await(Promise promise) : Base(std::forward<Promise>(promise)) { }

  /**
   * \brief Resumes the coroutine and returns the result.
   * \return The result of the promise.
   */
  auto await_resume() -> decltype(auto) { return await_resume_<Return>(0); }

private:
  template<class Return_, std::enable_if_t<std::is_copy_constructible_v<Return_>, bool> = true> Return_ await_resume_(int /*unused*/) {
    if (auto value = promise.state.ifResolved()) { return *value; }
    if (auto error = promise.state.ifRejected()) { std::rethrow_exception(*error); }
    throw std::runtime_error("Promise is still in pending state. Cannot resume.");
  }

  template<class Return_> auto await_resume_(unsigned /*unused*/) -> Return_&& {
    if (auto value = promise.state.ifResolved()) { return std::move(*value); }
    if (auto error = promise.state.ifRejected()) { std::rethrow_exception(*error); }
    throw std::runtime_error("Promise is still in pending state. Cannot resume.");
  }
};

/**
 * \brief Specialization of Await for Promise<void>.
 */
template<> struct Await<Promise<void>> : public AwaitBase<Promise<void>> {
  using Base = AwaitBase<Promise<void>>;

  /**
   * \brief Constructor with a promise.
   * \param promise_ The promise to await.
   */
  explicit Await(Promise<void> promise_) : Base(std::forward<Promise<void>>(promise_)) { }

  /**
   * \brief Resumes the coroutine.
   */
  void await_resume() { }
};

/**
 * \brief Specialization of Await for Promise<void>&.
 */
template<> struct Await<Promise<void>&> : public AwaitBase<Promise<void>&> {
  using Base = AwaitBase<Promise<void>&>;

  /**
   * \brief Constructor with a promise.
   * \param promise_ The promise to await.
   */
  explicit Await(Promise<void>& promise_) : Base(promise_) { }

  /**
   * \brief Resumes the coroutine.
   */
  void await_resume() { }
};
#endif
/**
 * \brief Base class for coroutine context.
 * \tparam Return The type of the value that the context will return.
 */
template<class Return> struct ContextBase : public Callback<Return> {
  using Base = Callback<Return>;
  using Base::promise;

  /**
   * \brief Default constructor.
   */
  ContextBase() : Base() { }

  ContextBase(ContextBase&&) = delete;
  auto operator=(ContextBase&&) = delete;
  ContextBase(const ContextBase&) = delete;
  auto operator=(const ContextBase&) = delete;
  ~ContextBase() = default;
#if CO2
  /**
   * \brief Gets the return object for the coroutine.
   * \param coroutine The coroutine handle.
   * \return The promise associated with the coroutine.
   */
  Promise<Return> get_return_object(co2::coroutine<>& coroutine)
#else
  /**
   * \brief Gets the return object for the coroutine.
   * \return The promise associated with the coroutine.
   */
  auto get_return_object() -> Promise<Return>
#endif
  {
    return Promise<Return>(static_cast<Callback<Return>*>(this));
  }
#if CO2
  /**
   * \brief Checks if the coroutine should initially suspend.
   * \return False, indicating the coroutine should not suspend.
   */
  bool initial_suspend() noexcept { return false; }

  /**
   * \brief Checks if the coroutine should finally suspend.
   * \return False, indicating the coroutine should not suspend.
   */
  bool final_suspend() noexcept { return false; }

  /**
   * \brief Checks if cancellation is requested.
   * \return False, indicating no cancellation is requested.
   */
  bool cancellation_requested() const noexcept { return false; }
#else
  /**
   * \brief Checks if the coroutine should initially suspend.
   * \return An instance of std::suspend_never, indicating the coroutine should
   * not suspend.
   */
  auto initial_suspend() noexcept -> std::suspend_never { return {}; }

  /**
   * \brief Checks if the coroutine should finally suspend.
   * \return An instance of std::suspend_never, indicating the coroutine should
   * not suspend.
   */
  auto final_suspend() noexcept -> std::suspend_never { return {}; }
#endif
#if !CO2
  /**
   * \brief Transforms a promise into an Await object.
   * \tparam T The type of the promise.
   * \param promise_ The promise to transform.
   * \return An Await object for the promise.
   */
  template<class T> auto await_transform(T&& promise_) -> Await<remove_rvalue_reference_t<T>> { return Await<remove_rvalue_reference_t<T>>(std::forward<T>(promise_)); }
#endif
  /**
   * \brief Handles unhandled exceptions in the coroutine.
   */
  void unhandled_exception() {
    if (promise != nullptr) {
      promise->state.resolved(std::current_exception());
      promise = nullptr;
    }
  }
};

/**
 * \brief Coroutine context for a specific return type.
 * \tparam Return The type of the value that the context will return.
 */
template<class Return> struct Context : public ContextBase<Return> {
  using Base = ContextBase<Return>;
  using Base::promise;

  /**
   * \brief Default constructor.
   */
  Context() = default;
#if CO2
  /**
   * \brief Sets the result of the coroutine.
   * \tparam T The type of the result.
   * \param value The result value.
   */
  template<class T> void set_result(T&& value)
#else
  /**
   * \brief Returns a value from the coroutine.
   * \tparam T The type of the value.
   * \param value The value to return.
   */
  template<class T> void return_value(T&& value)
#endif
  {
    Base::operator()(std::forward<T>(value));
  }
};

/**
 * \brief Specialization of Context for void.
 */
template<> struct Context<void> : public ContextBase<void> {
  using Base = ContextBase<void>;
  using Base::promise;

  /**
   * \brief Default constructor.
   */
  Context() = default;
#if CO2
  /**
   * \brief Sets the result of the coroutine.
   */
  void set_result()
#else
  /**
   * \brief Returns void from the coroutine.
   */
  void return_void()
#endif
  {
    Base::operator()();
  }
};
}  // namespace detail
}  // namespace promise
#if CO2
/**
 * \brief Coroutine traits specialization for promise::Promise.
 * \tparam Return The return type of the promise.
 */
template<class Return> struct co2::coroutine_traits<promise::Promise<Return>> {
  using promise_type = promise::detail::Context<Return>;
};
#else
/**
 * \brief Coroutine traits specialization for promise::Promise.
 * \tparam Return The return type of the promise.
 * \tparam Args The argument types for the coroutine.
 */
template<class Return, class... Args> struct std::coroutine_traits<promise::Promise<Return>, Args...> {  // NOLINT(cert-dcl58-cpp) justification: required to register coroutine traits for coroutine support
  using promise_type = promise::detail::Context<Return>;
};
#endif
#endif
