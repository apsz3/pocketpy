#pragma once

#include "ceval.h"

class RangeIter : public BaseIter {
    i64 current;
    pkpy::Range r;
public:
    RangeIter(VM* vm, PyVar _ref) : BaseIter(vm, _ref) {
        this->r = OBJ_GET(pkpy::Range, _ref);
        this->current = r.start;
    }

    inline bool _has_next(){
        return r.step > 0 ? current < r.stop : current > r.stop;
    }

    PyVar next(){
        if(!_has_next()) return nullptr;
        current += r.step;
        return vm->PyInt(current-r.step);
    }
};

template <typename T>
class ArrayIter : public BaseIter {
    size_t index = 0;
    const T* p;
public:
    ArrayIter(VM* vm, PyVar _ref) : BaseIter(vm, _ref) { p = &OBJ_GET(T, _ref);}
    PyVar next(){
        if(index == p->size()) return nullptr;
        return p->operator[](index++); 
    }
};

class StringIter : public BaseIter {
    int index = 0;
    Str* str;
public:
    StringIter(VM* vm, PyVar _ref) : BaseIter(vm, _ref) {
        str = &OBJ_GET(Str, _ref);
    }

    PyVar next() {
        if(index == str->u8_length()) return nullptr;
        return vm->PyStr(str->u8_getitem(index++));
    }
};

class Generator: public BaseIter {
    std::unique_ptr<Frame> frame;
    int state; // 0,1,2
public:
    Generator(VM* vm, std::unique_ptr<Frame>&& frame)
        : BaseIter(vm, nullptr), frame(std::move(frame)), state(0) {}

    PyVar next() {
        if(state == 2) return nullptr;
        vm->callstack.push(std::move(frame));
        PyVar ret = vm->_exec();
        if(ret == vm->_py_op_yield){
            frame = std::move(vm->callstack.top());
            vm->callstack.pop();
            state = 1;
            return frame->pop_value(vm);
        }else{
            state = 2;
            return nullptr;
        }
    }
};