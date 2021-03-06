// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOAD_ELIMINATION_H_
#define V8_COMPILER_LOAD_ELIMINATION_H_

#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Foward declarations.
struct FieldAccess;

class LoadElimination final : public AdvancedReducer {
 public:
  LoadElimination(Editor* editor, Zone* zone)
      : AdvancedReducer(editor), node_states_(zone) {}
  ~LoadElimination() final {}

  Reduction Reduce(Node* node) final;

 private:
  static const size_t kMaxTrackedElements = 8;

  // Abstract state to approximate the current state of an element along the
  // effect paths through the graph.
  class AbstractElements final : public ZoneObject {
   public:
    explicit AbstractElements(Zone* zone) {
      for (size_t i = 0; i < arraysize(elements_); ++i) {
        elements_[i] = Element();
      }
    }
    AbstractElements(Node* object, Node* index, Node* value, Zone* zone)
        : AbstractElements(zone) {
      elements_[next_index_++] = Element(object, index, value);
    }

    AbstractElements const* Extend(Node* object, Node* index, Node* value,
                                   Zone* zone) const {
      AbstractElements* that = new (zone) AbstractElements(*this);
      that->elements_[that->next_index_] = Element(object, index, value);
      that->next_index_ = (that->next_index_ + 1) % arraysize(elements_);
      return that;
    }
    Node* Lookup(Node* object, Node* index) const;
    AbstractElements const* Kill(Node* object, Node* index, Zone* zone) const;
    bool Equals(AbstractElements const* that) const;
    AbstractElements const* Merge(AbstractElements const* that,
                                  Zone* zone) const;

   private:
    struct Element {
      Element() {}
      Element(Node* object, Node* index, Node* value)
          : object(object), index(index), value(value) {}

      Node* object = nullptr;
      Node* index = nullptr;
      Node* value = nullptr;
    };

    Element elements_[kMaxTrackedElements];
    size_t next_index_ = 0;
  };

  // Abstract state to approximate the current state of a certain field along
  // the effect paths through the graph.
  class AbstractField final : public ZoneObject {
   public:
    explicit AbstractField(Zone* zone) : info_for_node_(zone) {}
    AbstractField(Node* object, Node* value, Zone* zone)
        : info_for_node_(zone) {
      info_for_node_.insert(std::make_pair(object, value));
    }

    AbstractField const* Extend(Node* object, Node* value, Zone* zone) const {
      AbstractField* that = new (zone) AbstractField(zone);
      that->info_for_node_ = this->info_for_node_;
      that->info_for_node_.insert(std::make_pair(object, value));
      return that;
    }
    Node* Lookup(Node* object) const;
    AbstractField const* Kill(Node* object, Zone* zone) const;
    bool Equals(AbstractField const* that) const {
      return this == that || this->info_for_node_ == that->info_for_node_;
    }
    AbstractField const* Merge(AbstractField const* that, Zone* zone) const {
      if (this->Equals(that)) return this;
      AbstractField* copy = new (zone) AbstractField(zone);
      for (auto this_it : this->info_for_node_) {
        Node* this_object = this_it.first;
        Node* this_value = this_it.second;
        auto that_it = that->info_for_node_.find(this_object);
        if (that_it != that->info_for_node_.end() &&
            that_it->second == this_value) {
          copy->info_for_node_.insert(this_it);
        }
      }
      return copy;
    }

   private:
    ZoneMap<Node*, Node*> info_for_node_;
  };

  static size_t const kMaxTrackedFields = 32;

  class AbstractState final : public ZoneObject {
   public:
    AbstractState() {
      for (size_t i = 0; i < arraysize(fields_); ++i) {
        fields_[i] = nullptr;
      }
    }

    bool Equals(AbstractState const* that) const;
    void Merge(AbstractState const* that, Zone* zone);

    AbstractState const* AddField(Node* object, size_t index, Node* value,
                                  Zone* zone) const;
    AbstractState const* KillField(Node* object, size_t index,
                                   Zone* zone) const;
    Node* LookupField(Node* object, size_t index) const;

    AbstractState const* AddElement(Node* object, Node* index, Node* value,
                                    Zone* zone) const;
    AbstractState const* KillElement(Node* object, Node* index,
                                     Zone* zone) const;
    Node* LookupElement(Node* object, Node* index) const;

   private:
    AbstractElements const* elements_ = nullptr;
    AbstractField const* fields_[kMaxTrackedFields];
  };

  class AbstractStateForEffectNodes final : public ZoneObject {
   public:
    explicit AbstractStateForEffectNodes(Zone* zone) : info_for_node_(zone) {}
    AbstractState const* Get(Node* node) const;
    void Set(Node* node, AbstractState const* state);

    Zone* zone() const { return info_for_node_.get_allocator().zone(); }

   private:
    ZoneVector<AbstractState const*> info_for_node_;
  };

  Reduction ReduceCheckMaps(Node* node);
  Reduction ReduceTransitionElementsKind(Node* node);
  Reduction ReduceLoadField(Node* node);
  Reduction ReduceStoreField(Node* node);
  Reduction ReduceLoadElement(Node* node);
  Reduction ReduceStoreElement(Node* node);
  Reduction ReduceEffectPhi(Node* node);
  Reduction ReduceStart(Node* node);
  Reduction ReduceOtherNode(Node* node);

  Reduction UpdateState(Node* node, AbstractState const* state);

  AbstractState const* ComputeLoopState(Node* node,
                                        AbstractState const* state) const;

  static int FieldIndexOf(FieldAccess const& access);

  AbstractState const* empty_state() const { return &empty_state_; }
  Zone* zone() const { return node_states_.zone(); }

  AbstractState const empty_state_;
  AbstractStateForEffectNodes node_states_;

  DISALLOW_COPY_AND_ASSIGN(LoadElimination);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOAD_ELIMINATION_H_
