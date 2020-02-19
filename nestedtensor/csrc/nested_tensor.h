#pragma once
#include <nested_node.h>

namespace torch {
namespace nested_tensor {

// TODO: Eventually allow construction from a list of _BufferNestedTensors.
struct NestedTensor {
  NestedTensor() = delete;
  NestedTensor(TensorNode&& structure);
  NestedTensor::NestedTensor(at::Tensor&& buffer, SizeNode nested_size);
  c10::optional<at::Tensor> get_buffer() {
    return _buffer;
  }
  const c10::optional<at::Tensor> get_buffer() const {
    return _buffer;
  }
  std::vector<c10::optional<int64_t>> size() {
    SizeNode tmp =
        map([](c10::IValue e) { return e.toIntList(); }, nested_size());
    return construct_size(tmp);
  }
  int64_t element_size() {
    return _first_variable.element_size();
  }
  SizeNode nested_size() {
    return map(
        [](at::Tensor tensor) { return c10::List<int64_t>(tensor.sizes()); },
        _structure);
  }
  SizeNode nested_stride() {
    return map(
        [](at::Tensor tensor) { return c10::List<int64_t>(tensor.strides()); },
        _structure);
  }
  NestedTensor pin_memory() {
    // NOTE: The assumption here is that pin_memory will materialize
    // the views that _structure contains when NestedTensor is contiguous.
    return NestedTensor(
        map([](at::Tensor tensor) { return tensor.pin_memory(); }, _structure));
  }
  NestedTensor grad() {
    if (is_contiguous()) {
      // NOTE: TensorNodes are based on split if contiguous. Any backward
      // performed on those will accumulate in the buffer's grad. What we're
      // creating here are views into the grad, which could then be used
      // further.
      TensorNode grad_tensor_node =
          build_structure(grad_buffer, _nested_size, _nested_stride);
      return NestedTensor(std::move(grad_tensor_node));
    }
    return NestedTensor(
        map([](at::Tensor tensor) { return tensor.grad(); }, _structure));
  }
  NestedTensor detach() {
    // NOTE: For the contiguous case the tensors in _structure are views
    // of parts of _buffer and the returned detached views will still
    // modify that buffer if using in-place methods etc.
    return NestedTensor(
        map([](at::Tensor tensor) { return tensor.detach(); }, _structure));
  }
  NestedTensor requires_grad_(bool requires_grad) {
    apply(
        [requires_grad](at::Tensor tensor) -> void {
          tensor.set_requires_grad(requires_grad);
        },
        _structure);
    if (is_contiguous()) {
      _buffer.set_requires_grad(requires_grad);
    }
    return *this;
  }
  void backward(NestedTensor gradient, bool retain_graph, bool create_graph) {
    if (is_contiguous() && gradient.is_contiguous()) {
      _buffer.backward(gradient.get_buffer(), retain_graph, create_graph);
    } else {
      apply(
          [retain_graph, create_graph](
              at::Tensor tensor1, at::Tensor tensor2) -> void {
            tensor1.backward(tensor2, retain_graph, create_graph);
          },
          _structure,
          gradient.get_structure());
    }
  }
  int64_t __len__() {
    return _structure.degree();
  }
  at::Tensor to_tensor() {
    if (is_contiguous()) {
      std::vector<int64_t> new_size;
      for (const auto& si : size()) {
        if (!si) {
          throw std::runtime_error(
              "to_tensor only works if none of size() is None.");
        }
        new_size.push_back(*si);
      }
      return _buffer.reshape(at::IntArrayRef(new_size));
    }
    return stack(flatten(_structure).vec());
  }
  int64_t nested_dim() {
    return _structure.height();
  }
  at::ScalarType scalar_type() {
    return _first_variable.scalar_type();
  }
  at::Backend backend() {
    return options().backend();
  }
  at::Device device() {
    return _first_variable.device();
  }
  at::TensorOptions options() {
    return _first_variable.options();
  }
  bool requires_grad() {
    return _first_variable.requires_grad();
  }
  int64_t dim() {
    return _first_variable.dim() + nested_dim();
  }
  int64_t numel() {
    auto fn = [](at::Tensor leaf, int64_t input) {
      return input + leaf.numel();
    };
    return reduce<decltype(fn), int64_t, at::Tensor>(_structure, fn, 0);
  }
  bool is_pinned() {
    if (is_contiguous()) {
      return _buffer.is_pinned();
    } else {
      return _first_variable.is_pinned();
    }
  }
  bool is_contiguous() const {
    // NOTE: The Tensors themselves might not be contiguous even if there is a
    // buffer. For this to be contiguous not only the individuals Tensors have
    // to be but also the buffer.
    auto fn = [](at::Tensor leaf, bool input) {
      return input && leaf.is_contiguous();
    };
    return _buffer.is_contiguous() &&
        reduce<decltype(fn), bool, at::Tensor>(_structure, fn, true);
  }
  NestedTensor contiguous();
  TensorNode& get_structure() {
    return _structure;
  }
  const TensorNode& get_structure() const {
    return _structure;
  }
  // TODO: Implement these and call into them isntead of implementing them
  // separately in Variable dispatch functions.
  // NestedTensor to - it's a pain due to the 100s of to overloads
  // separately in Variable dispatch functions.

 private:
  c10::optional<at::Tensor> _buffer;
  TensorNode _structure;
  at::Tensor _first_variable;
};
} // namespace nested_tensor
} // namespace torch
