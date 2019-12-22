export module util.value_ptr;

import <memory>;

export template <typename T>
class value_ptr {
 public:
  value_ptr(std::unique_ptr<T> value) : contents_(std::move(value)) {}

  value_ptr() = default;
  value_ptr(value_ptr&&) = default;
  value_ptr& operator=(value_ptr&&) = default;

  value_ptr(const value_ptr& other) {
    if (other.contents_) {
      contents_ = std::make_unique<T>(*other.contents_);
    }
  }

  value_ptr& operator=(const value_ptr& other) {
    if (other.contents_) {
      contents_ = std::make_unique<T>(*other.contents_);
    }
    return *this;
  }

  T* get() const { return contents_.get(); }

  T& operator*() const { return *contents_; }
  T* operator->() const { return contents_.get(); }

 private:
  std::unique_ptr<T> contents_ = nullptr;
};

export template <typename T, typename... Args>
value_ptr<T> make_value(Args&&... args) {
  static_assert(std::is_constructible_v<T, Args...>);
  return value_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));
}
