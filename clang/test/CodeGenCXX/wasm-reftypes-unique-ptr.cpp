// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 -disable-llvm-passes | FileCheck %s
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 -disable-llvm-passes | FileCheck %s

// An __externref_t can be owned by a unique_ptr-like smart pointer: the
// pointer member is an __externref_t *, i.e. an __externref_table slot index
// (`ptr addrspace(2)` in IR) stored in an ordinary linear-memory struct.
// `new __externref_t` inside make_unique allocates a table slot via the
// compiler-rt allocator, `delete` in default_delete releases it, and `*ptr`
// yields an __externref_t& that loads/stores through the slot.

// Minimal stand-in for std::unique_ptr; <memory> is not available in a cc1 test.
namespace std {

template <typename T> struct remove_reference { typedef T type; };
template <typename T> struct remove_reference<T &> { typedef T type; };
template <typename T> struct remove_reference<T &&> { typedef T type; };

template <typename T>
typename remove_reference<T>::type &&move(T &&t) {
  return static_cast<typename remove_reference<T>::type &&>(t);
}

template <typename T> struct default_delete {
  void operator()(T *p) const { delete p; }
};

template <typename T, typename D = default_delete<T>> class unique_ptr {
  T *ptr;

public:
  unique_ptr() : ptr(nullptr) {}
  explicit unique_ptr(T *p) : ptr(p) {}
  unique_ptr(unique_ptr &&o) : ptr(o.ptr) { o.ptr = nullptr; }
  unique_ptr &operator=(unique_ptr &&o) {
    if (this != &o) {
      reset(o.ptr);
      o.ptr = nullptr;
    }
    return *this;
  }
  unique_ptr(const unique_ptr &) = delete;
  unique_ptr &operator=(const unique_ptr &) = delete;
  ~unique_ptr() { reset(); }

  T &operator*() const { return *ptr; }
  T *operator->() const { return ptr; }
  T *get() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }

  T *release() {
    T *p = ptr;
    ptr = nullptr;
    return p;
  }
  void reset(T *p = nullptr) {
    T *old = ptr;
    ptr = p;
    if (old)
      D()(old);
  }
};

template <typename T> unique_ptr<T> make_unique() {
  return unique_ptr<T>(new T());
}

} // namespace std

typedef std::unique_ptr<__externref_t> ref_owner_t;

// Takes ownership of the externref.
void consume(ref_owner_t r) { __externref_t v = *r; (void)v; }

// Borrows the externref; caller keeps ownership.
void inspect(const ref_owner_t &r) { (void)r.get(); }

ref_owner_t make_ref() { return std::make_unique<__externref_t>(); }

ref_owner_t g_ref;

int main() {
  ref_owner_t a = make_ref();
  inspect(a);

  ref_owner_t b = std::move(a);
  consume(std::move(b));

  g_ref = make_ref();
  __externref_t *raw = g_ref.release();
  g_ref.reset(raw);
  g_ref.reset();

  return 0;
}

// The unique_ptr<__externref_t> is a plain struct holding one pointer.
// CHECK: %"class.std::unique_ptr" = type { ptr addrspace(2) }

// Dereferencing the owned pointer reads the externref out of its table slot.
// CHECK-LABEL: define{{.*}} void @_Z7consumeSt10unique_ptrIu11externref_tSt14default_deleteIu11externref_tEE(ptr noundef %r)
// CHECK:         [[REF:%.*]] = call {{.*}} ptr addrspace(2) @_ZNKSt10unique_ptrIu11externref_tSt14default_deleteIu11externref_tEEdeEv(
// CHECK:         load target("wasm.externref"), ptr addrspace(2) [[REF]], align 1

// make_unique<__externref_t> allocates one table slot; no operator new.
// CHECK-LABEL: define{{.*}} void @_ZSt11make_uniqueIu11externref_tESt10unique_ptrIT_St14default_deleteIS1_EEv(
// CHECK:         call ptr addrspace(2) @__externref_table_alloc({{i32|i64}} 1)
// CHECK-NOT:     @_Znw
// CHECK:       declare {{.*}} ptr addrspace(2) @__externref_table_alloc({{i32|i64}} noundef)

// default_delete<__externref_t> releases the slot; no operator delete.
// CHECK-LABEL: define{{.*}} void @_ZNKSt14default_deleteIu11externref_tEclEPu11externref_t(
// CHECK:         call void @__externref_table_free(ptr addrspace(2) %{{.*}})
// CHECK-NOT:     @_Zdl
// CHECK:       declare void @__externref_table_free(ptr addrspace(2) noundef)

