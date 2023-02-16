#pragma once

#include "frame.h"
#include "error.h"

#define DEF_NATIVE(type, ctype, ptype)                          \
    inline ctype& Py##type##_AS_C(const PyVar& obj) {           \
        check_type(obj, ptype);                                 \
        return OBJ_GET(ctype, obj);                             \
    }                                                           \
    inline PyVar Py##type(const ctype& value) { return new_object(ptype, value);} \
    inline PyVar Py##type(ctype&& value) { return new_object(ptype, std::move(value));}

class Generator;

class VM {
public:
    std::stack< std::unique_ptr<Frame> > callstack;
    PyVar _py_op_call;
    PyVar _py_op_yield;
    // PyVar _ascii_str_pool[128];

    PyVar run_frame(Frame* frame){
        while(frame->has_next_bytecode()){
            const Bytecode& byte = frame->next_bytecode();
            // if(true || frame->_module != builtins){
            //     printf("%d: %s (%d)\n",frame->_ip, OP_NAMES[byte.op], byte.arg);
            // }
            switch (byte.op)
            {
            case OP_NO_OP: break;       // do nothing
            case OP_LOAD_CONST: frame->push(frame->co->consts[byte.arg]); break;
            case OP_LOAD_LAMBDA: {
                PyVar obj = frame->co->consts[byte.arg];
                setattr(obj, __module__, frame->_module);
                frame->push(obj);
            } break;
            case OP_LOAD_NAME_REF: {
                frame->push(PyRef(NameRef(frame->co->names[byte.arg])));
            } break;
            case OP_LOAD_NAME: {
                frame->push(NameRef(frame->co->names[byte.arg]).get(this, frame));
            } break;
            case OP_STORE_NAME: {
                auto& p = frame->co->names[byte.arg];
                NameRef(p).set(this, frame, frame->pop_value(this));
            } break;
            case OP_BUILD_ATTR: {
                int name = byte.arg >> 1;
                bool _rvalue = byte.arg % 2 == 1;
                auto& attr = frame->co->names[name];
                PyVar obj = frame->pop_value(this);
                AttrRef ref = AttrRef(obj, NameRef(attr));
                if(_rvalue) frame->push(ref.get(this, frame));
                else frame->push(PyRef(ref));
            } break;
            case OP_BUILD_INDEX: {
                PyVar index = frame->pop_value(this);
                auto ref = IndexRef(frame->pop_value(this), index);
                if(byte.arg == 0) frame->push(PyRef(ref));
                else frame->push(ref.get(this, frame));
            } break;
            case OP_STORE_REF: {
                PyVar obj = frame->pop_value(this);
                PyVarRef r = frame->pop();
                PyRef_AS_C(r)->set(this, frame, std::move(obj));
            } break;
            case OP_DELETE_REF: {
                PyVarRef r = frame->pop();
                PyRef_AS_C(r)->del(this, frame);
            } break;
            case OP_BUILD_SMART_TUPLE:
            {
                pkpy::Args items = frame->pop_n_reversed(byte.arg);
                bool done = false;
                for(int i=0; i<items.size(); i++){
                    if(!items[i]->is_type(tp_ref)) {
                        done = true;
                        for(int j=i; j<items.size(); j++) frame->try_deref(this, items[j]);
                        frame->push(PyTuple(std::move(items)));
                        break;
                    }
                }
                if(done) break;
                frame->push(PyRef(TupleRef(std::move(items))));
            } break;
            case OP_BUILD_STRING:
            {
                pkpy::Args items = frame->pop_n_values_reversed(this, byte.arg);
                StrStream ss;
                for(int i=0; i<items.size(); i++) ss << PyStr_AS_C(asStr(items[i]));
                frame->push(PyStr(ss.str()));
            } break;
            case OP_LOAD_EVAL_FN: {
                frame->push(builtins->attr(m_eval));
            } break;
            case OP_LIST_APPEND: {
                pkpy::Args args(2);
                args[1] = frame->pop_value(this);            // obj
                args[0] = frame->top_value_offset(this, -2);     // list
                fast_call(m_append, std::move(args));
            } break;
            case OP_STORE_FUNCTION:
                {
                    PyVar obj = frame->pop_value(this);
                    const pkpy::Function_& fn = PyFunction_AS_C(obj);
                    setattr(obj, __module__, frame->_module);
                    frame->f_globals()[fn->name] = obj;
                } break;
            case OP_BUILD_CLASS:
                {
                    const Str& clsName = frame->co->names[byte.arg].first;
                    PyVar clsBase = frame->pop_value(this);
                    if(clsBase == None) clsBase = _t(tp_object);
                    check_type(clsBase, tp_type);
                    PyVar cls = new_type_object(frame->_module, clsName, clsBase);
                    while(true){
                        PyVar fn = frame->pop_value(this);
                        if(fn == None) break;
                        const pkpy::Function_& f = PyFunction_AS_C(fn);
                        setattr(fn, __module__, frame->_module);
                        setattr(cls, f->name, fn);
                    }
                } break;
            case OP_RETURN_VALUE: return frame->pop_value(this);
            case OP_PRINT_EXPR:
                {
                    const PyVar expr = frame->top_value(this);
                    if(expr == None) break;
                    *_stdout << PyStr_AS_C(asRepr(expr)) << '\n';
                } break;
            case OP_POP_TOP: frame->_pop(); break;
            case OP_BINARY_OP:
                {
                    pkpy::Args args(2);
                    args[1] = frame->pop_value(this);
                    args[0] = frame->top_value(this);
                    frame->top() = fast_call(BINARY_SPECIAL_METHODS[byte.arg], std::move(args));
                } break;
            case OP_BITWISE_OP:
                {
                    frame->push(
                        fast_call(BITWISE_SPECIAL_METHODS[byte.arg],
                        frame->pop_n_values_reversed(this, 2))
                    );
                } break;
            case OP_COMPARE_OP:
                {
                    pkpy::Args args(2);
                    args[1] = frame->pop_value(this);
                    args[0] = frame->top_value(this);
                    frame->top() = fast_call(CMP_SPECIAL_METHODS[byte.arg], std::move(args));
                } break;
            case OP_IS_OP:
                {
                    PyVar rhs = frame->pop_value(this);
                    bool ret_c = rhs == frame->top_value(this);
                    if(byte.arg == 1) ret_c = !ret_c;
                    frame->top() = PyBool(ret_c);
                } break;
            case OP_CONTAINS_OP:
                {
                    PyVar rhs = frame->pop_value(this);
                    bool ret_c = PyBool_AS_C(call(rhs, __contains__, pkpy::one_arg(frame->pop_value(this))));
                    if(byte.arg == 1) ret_c = !ret_c;
                    frame->push(PyBool(ret_c));
                } break;
            case OP_UNARY_NEGATIVE:
                frame->top() = num_negated(frame->top_value(this));
                break;
            case OP_UNARY_NOT:
                {
                    PyVar obj = frame->pop_value(this);
                    const PyVar& obj_bool = asBool(obj);
                    frame->push(PyBool(!PyBool_AS_C(obj_bool)));
                } break;
            case OP_POP_JUMP_IF_FALSE:
                if(!PyBool_AS_C(asBool(frame->pop_value(this)))) frame->jump_abs(byte.arg);
                break;
            case OP_LOAD_NONE: frame->push(None); break;
            case OP_LOAD_TRUE: frame->push(True); break;
            case OP_LOAD_FALSE: frame->push(False); break;
            case OP_LOAD_ELLIPSIS: frame->push(Ellipsis); break;
            case OP_ASSERT:
                {
                    PyVar _msg = frame->pop_value(this);
                    Str msg = PyStr_AS_C(asStr(_msg));
                    PyVar expr = frame->pop_value(this);
                    if(asBool(expr) != True) _error("AssertionError", msg);
                } break;
            case OP_EXCEPTION_MATCH:
                {
                    const auto& _e = PyException_AS_C(frame->top());
                    Str name = frame->co->names[byte.arg].first;
                    frame->push(PyBool(_e.match_type(name)));
                } break;
            case OP_RAISE:
                {
                    PyVar obj = frame->pop_value(this);
                    Str msg = obj == None ? "" : PyStr_AS_C(asStr(obj));
                    Str type = frame->co->names[byte.arg].first;
                    _error(type, msg);
                } break;
            case OP_RE_RAISE: _raise(); break;
            case OP_BUILD_LIST:
                frame->push(PyList(
                    frame->pop_n_values_reversed(this, byte.arg).to_list()));
                break;
            case OP_BUILD_MAP:
                {
                    pkpy::Args items = frame->pop_n_values_reversed(this, byte.arg*2);
                    PyVar obj = call(builtins->attr("dict"));
                    for(int i=0; i<items.size(); i+=2){
                        call(obj, __setitem__, pkpy::two_args(items[i], items[i+1]));
                    }
                    frame->push(obj);
                } break;
            case OP_BUILD_SET:
                {
                    PyVar list = PyList(
                        frame->pop_n_values_reversed(this, byte.arg).to_list()
                    );
                    PyVar obj = call(builtins->attr("set"), pkpy::one_arg(list));
                    frame->push(obj);
                } break;
            case OP_DUP_TOP: frame->push(frame->top_value(this)); break;
            case OP_CALL:
                {
                    int ARGC = byte.arg & 0xFFFF;
                    int KWARGC = (byte.arg >> 16) & 0xFFFF;
                    pkpy::Args kwargs(0);
                    if(KWARGC > 0) kwargs = frame->pop_n_values_reversed(this, KWARGC*2);
                    pkpy::Args args = frame->pop_n_values_reversed(this, ARGC);
                    PyVar callable = frame->pop_value(this);
                    PyVar ret = call(callable, std::move(args), kwargs, true);
                    if(ret == _py_op_call) return ret;
                    frame->push(std::move(ret));
                } break;
            case OP_JUMP_ABSOLUTE: frame->jump_abs(byte.arg); break;
            case OP_SAFE_JUMP_ABSOLUTE: frame->jump_abs_safe(byte.arg); break;
            case OP_GOTO: {
                const Str& label = frame->co->names[byte.arg].first;
                int* target = frame->co->labels.try_get(label);
                if(target == nullptr) _error("KeyError", "label '" + label + "' not found");
                frame->jump_abs_safe(*target);
            } break;
            case OP_GET_ITER:
                {
                    PyVar obj = frame->pop_value(this);
                    PyVar iter_obj = nullptr;
                    if(!obj->is_type(tp_native_iterator)){
                        PyVarOrNull iter_f = getattr(obj, __iter__, false);
                        if(iter_f != nullptr) iter_obj = call(iter_f);
                    }else{
                        iter_obj = obj;
                    }
                    if(iter_obj == nullptr){
                        TypeError(OBJ_NAME(_t(obj)).escape(true) + " object is not iterable");
                    }
                    PyVarRef var = frame->pop();
                    check_type(var, tp_ref);
                    PyIter_AS_C(iter_obj)->var = var;
                    frame->push(std::move(iter_obj));
                } break;
            case OP_FOR_ITER:
                {
                    // top() must be PyIter, so no need to try_deref()
                    auto& it = PyIter_AS_C(frame->top());
                    PyVar obj = it->next();
                    if(obj != nullptr){
                        PyRef_AS_C(it->var)->set(this, frame, std::move(obj));
                    }else{
                        int blockEnd = frame->co->blocks[byte.block].end;
                        frame->jump_abs_safe(blockEnd);
                    }
                } break;
            case OP_LOOP_CONTINUE:
                {
                    int blockStart = frame->co->blocks[byte.block].start;
                    frame->jump_abs(blockStart);
                } break;
            case OP_LOOP_BREAK:
                {
                    int blockEnd = frame->co->blocks[byte.block].end;
                    frame->jump_abs_safe(blockEnd);
                } break;
            case OP_JUMP_IF_FALSE_OR_POP:
                {
                    const PyVar expr = frame->top_value(this);
                    if(asBool(expr)==False) frame->jump_abs(byte.arg);
                    else frame->pop_value(this);
                } break;
            case OP_JUMP_IF_TRUE_OR_POP:
                {
                    const PyVar expr = frame->top_value(this);
                    if(asBool(expr)==True) frame->jump_abs(byte.arg);
                    else frame->pop_value(this);
                } break;
            case OP_BUILD_SLICE:
                {
                    PyVar stop = frame->pop_value(this);
                    PyVar start = frame->pop_value(this);
                    pkpy::Slice s;
                    if(start != None) {check_type(start, tp_int); s.start = (int)PyInt_AS_C(start);}
                    if(stop != None) {check_type(stop, tp_int); s.stop = (int)PyInt_AS_C(stop);}
                    frame->push(PySlice(s));
                } break;
            case OP_IMPORT_NAME:
                {
                    const Str& name = frame->co->names[byte.arg].first;
                    auto it = _modules.find(name);
                    if(it == _modules.end()){
                        auto it2 = _lazy_modules.find(name);
                        if(it2 == _lazy_modules.end()){
                            _error("ImportError", "module '" + name + "' not found");
                        }else{
                            const Str& source = it2->second;
                            CodeObject_ code = compile(source, name, EXEC_MODE);
                            PyVar _m = new_module(name);
                            _exec(code, _m, pkpy::make_shared<pkpy::NameDict>());
                            frame->push(_m);
                            _lazy_modules.erase(it2);
                        }
                    }else{
                        frame->push(it->second);
                    }
                } break;
            case OP_YIELD_VALUE: return _py_op_yield;
            // TODO: using "goto" inside with block may cause __exit__ not called
            case OP_WITH_ENTER: call(frame->pop_value(this), __enter__); break;
            case OP_WITH_EXIT: call(frame->pop_value(this), __exit__); break;
            case OP_TRY_BLOCK_ENTER: frame->on_try_block_enter(); break;
            case OP_TRY_BLOCK_EXIT: frame->on_try_block_exit(); break;
            default:
                throw std::runtime_error(Str("opcode ") + OP_NAMES[byte.op] + " is not implemented");
                break;
            }
        }

        if(frame->co->src->mode == EVAL_MODE || frame->co->src->mode == JSON_MODE){
            if(frame->_data.size() != 1) throw std::runtime_error("_data.size() != 1 in EVAL/JSON_MODE");
            return frame->pop_value(this);
        }

        if(!frame->_data.empty()) throw std::runtime_error("_data.size() != 0 in EXEC_MODE");
        return None;
    }

    pkpy::NameDict _types;
    pkpy::NameDict _modules;                             // loaded modules
    emhash8::HashMap<Str, Str> _lazy_modules;     // lazy loaded modules
    PyVar None, True, False, Ellipsis;

    bool use_stdio;
    std::ostream* _stdout;
    std::ostream* _stderr;

    PyVar builtins;         // builtins module
    PyVar _main;            // __main__ module

    int maxRecursionDepth = 1000;

    VM(bool use_stdio){
        this->use_stdio = use_stdio;
        if(use_stdio){
            this->_stdout = &std::cout;
            this->_stderr = &std::cerr;
        }else{
            this->_stdout = new StrStream();
            this->_stderr = new StrStream();
        }

        init_builtin_types();
        // for(int i=0; i<128; i++) _ascii_str_pool[i] = new_object(tp_str, std::string(1, (char)i));
    }

    PyVar asStr(const PyVar& obj){
        PyVarOrNull f = getattr(obj, __str__, false);
        if(f != nullptr) return call(f);
        return asRepr(obj);
    }

    inline Frame* top_frame() const {
        if(callstack.empty()) UNREACHABLE();
        return callstack.top().get();
    }

    PyVar asRepr(const PyVar& obj){
        if(obj->is_type(tp_type)) return PyStr("<class '" + OBJ_GET(Str, obj->attr(__name__)) + "'>");
        return call(obj, __repr__);
    }

    const PyVar& asBool(const PyVar& obj){
        if(obj->is_type(tp_bool)) return obj;
        if(obj == None) return False;
        if(obj->is_type(tp_int)) return PyBool(PyInt_AS_C(obj) != 0);
        if(obj->is_type(tp_float)) return PyBool(PyFloat_AS_C(obj) != 0.0);
        PyVarOrNull len_fn = getattr(obj, __len__, false);
        if(len_fn != nullptr){
            PyVar ret = call(len_fn);
            return PyBool(PyInt_AS_C(ret) > 0);
        }
        return True;
    }

    PyVar fast_call(const Str& name, pkpy::Args&& args){
        PyObject* cls = _t(args[0]).get();
        while(cls != None.get()) {
            PyVar* val = cls->attr().try_get(name);
            if(val != nullptr) return call(*val, std::move(args));
            cls = cls->attr(__base__).get();
        }
        AttributeError(args[0], name);
        return nullptr;
    }

    inline PyVar call(const PyVar& _callable){
        return call(_callable, pkpy::no_arg(), pkpy::no_arg(), false);
    }

    template<typename ArgT>
    inline std::enable_if_t<std::is_same_v<RAW(ArgT), pkpy::Args>, PyVar>
    call(const PyVar& _callable, ArgT&& args){
        return call(_callable, std::forward<ArgT>(args), pkpy::no_arg(), false);
    }

    template<typename ArgT>
    inline std::enable_if_t<std::is_same_v<RAW(ArgT), pkpy::Args>, PyVar>
    call(const PyVar& obj, const Str& func, ArgT&& args){
        return call(getattr(obj, func), std::forward<ArgT>(args), pkpy::no_arg(), false);
    }

    inline PyVar call(const PyVar& obj, const Str& func){
        return call(getattr(obj, func), pkpy::no_arg(), pkpy::no_arg(), false);
    }

    PyVar call(const PyVar& _callable, pkpy::Args args, const pkpy::Args& kwargs, bool opCall){
        if(_callable->is_type(tp_type)){
            PyVar* new_f = _callable->attr().try_get(__new__);
            PyVar obj;
            if(new_f != nullptr){
                obj = call(*new_f, args, kwargs, false);
            }else{
                obj = new_object(_callable, DUMMY_VAL);
                PyVarOrNull init_f = getattr(obj, __init__, false);
                if (init_f != nullptr) call(init_f, args, kwargs, false);
            }
            return obj;
        }

        const PyVar* callable = &_callable;
        if((*callable)->is_type(tp_bound_method)){
            auto& bm = PyBoundMethod_AS_C((*callable));
            callable = &bm.method;      // get unbound method
            args.extend_self(bm.obj);
        }

        if((*callable)->is_type(tp_native_function)){
            const auto& f = OBJ_GET(pkpy::NativeFunc, *callable);
            if(kwargs.size() != 0) TypeError("native_function does not accept keyword arguments");
            return f(this, args);
        } else if((*callable)->is_type(tp_function)){
            const pkpy::Function_& fn = PyFunction_AS_C((*callable));
            pkpy::shared_ptr<pkpy::NameDict> _locals = pkpy::make_shared<pkpy::NameDict>();
            pkpy::NameDict& locals = *_locals;

            int i = 0;
            for(const auto& name : fn->args){
                if(i < args.size()){
                    locals.emplace(name, args[i++]);
                    continue;
                }
                TypeError("missing positional argument '" + name + "'");
            }

            locals.insert(fn->kwArgs.begin(), fn->kwArgs.end());

            std::vector<Str> positional_overrided_keys;
            if(!fn->starredArg.empty()){
                pkpy::List vargs;        // handle *args
                while(i < args.size()) vargs.push_back(args[i++]);
                locals.emplace(fn->starredArg, PyTuple(std::move(vargs)));
            }else{
                for(const auto& key : fn->kwArgsOrder){
                    if(i < args.size()){
                        locals[key] = args[i++];
                        positional_overrided_keys.push_back(key);
                    }else{
                        break;
                    }
                }
                if(i < args.size()) TypeError("too many arguments");
            }

            for(int i=0; i<kwargs.size(); i+=2){
                const Str& key = PyStr_AS_C(kwargs[i]);
                if(!fn->kwArgs.contains(key)){
                    TypeError(key.escape(true) + " is an invalid keyword argument for " + fn->name + "()");
                }
                const PyVar& val = kwargs[i+1];
                if(!positional_overrided_keys.empty()){
                    auto it = std::find(positional_overrided_keys.begin(), positional_overrided_keys.end(), key);
                    if(it != positional_overrided_keys.end()){
                        TypeError("multiple values for argument '" + key + "'");
                    }
                }
                locals[key] = val;
            }

            PyVar* _m = (*callable)->attr().try_get(__module__);
            PyVar _module = _m != nullptr ? *_m : top_frame()->_module;
            auto _frame = _new_frame(fn->code, _module, _locals);
            if(fn->code->is_generator){
                return PyIter(pkpy::make_shared<BaseIter, Generator>(
                    this, std::move(_frame)));
            }
            callstack.push(std::move(_frame));
            if(opCall) return _py_op_call;
            return _exec();
        }
        TypeError("'" + OBJ_NAME(_t(*callable)) + "' object is not callable");
        return None;
    }


    // repl mode is only for setting `frame->id` to 0
    PyVarOrNull exec(Str source, Str filename, CompileMode mode, PyVar _module=nullptr){
        if(_module == nullptr) _module = _main;
        try {
            CodeObject_ code = compile(source, filename, mode);
            return _exec(code, _module, pkpy::make_shared<pkpy::NameDict>());
        }catch (const pkpy::Exception& e){
            *_stderr << e.summary() << '\n';
        }
        catch (const std::exception& e) {
            *_stderr << "A std::exception occurred! It may be a bug, please report it!!\n";
            *_stderr << e.what() << '\n';
        }
        callstack = {};
        return nullptr;
    }

    template<typename ...Args>
    inline std::unique_ptr<Frame> _new_frame(Args&&... args){
        if(callstack.size() > maxRecursionDepth){
            _error("RecursionError", "maximum recursion depth exceeded");
        }
        return std::make_unique<Frame>(std::forward<Args>(args)...);
    }

    template<typename ...Args>
    inline PyVar _exec(Args&&... args){
        callstack.push(_new_frame(std::forward<Args>(args)...));
        return _exec();
    }

    PyVar _exec(){
        Frame* frame = top_frame();
        i64 base_id = frame->id;
        PyVar ret = nullptr;
        bool need_raise = false;

        while(true){
            if(frame->id < base_id) UNREACHABLE();
            try{
                if(need_raise){ need_raise = false; _raise(); }
                ret = run_frame(frame);
                if(ret == _py_op_yield) return _py_op_yield;
                if(ret != _py_op_call){
                    if(frame->id == base_id){      // [ frameBase<- ]
                        callstack.pop();
                        return ret;
                    }else{
                        callstack.pop();
                        frame = callstack.top().get();
                        frame->push(ret);
                    }
                }else{
                    frame = callstack.top().get();  // [ frameBase, newFrame<- ]
                }
            }catch(HandledException& e){
                continue;
            }catch(UnhandledException& e){
                PyVar obj = frame->pop();
                pkpy::Exception& _e = PyException_AS_C(obj);
                _e.st_push(frame->snapshot());
                callstack.pop();
                if(callstack.empty()) throw _e;
                frame = callstack.top().get();
                frame->push(obj);
                if(frame->id < base_id) throw ToBeRaisedException();
                need_raise = true;
            }catch(ToBeRaisedException& e){
                need_raise = true;
            }
        }
    }

    std::vector<PyVar> _all_types;

    PyVar new_type_object(PyVar mod, Str name, PyVar base){
        if(!base->is_type(tp_type)) UNREACHABLE();
        PyVar obj = pkpy::make_shared<PyObject, Py_<Type>>(tp_type, _all_types.size());
        setattr(obj, __base__, base);
        Str fullName = name;
        if(mod != builtins) fullName = OBJ_NAME(mod) + "." + name;
        setattr(obj, __name__, PyStr(fullName));
        setattr(mod, name, obj);
        _all_types.push_back(obj);
        return obj;
    }

    Type _new_type_object(Str name, Type base=0) {
        PyVar obj = pkpy::make_shared<PyObject, Py_<Type>>(tp_type, _all_types.size());
        setattr(obj, __base__, _t(base));
        _types[name] = obj;
        _all_types.push_back(obj);
        return OBJ_GET(Type, obj);
    }

    template<typename T>
    inline PyVar new_object(const PyVar& type, const T& _value) {
        if(!type->is_type(tp_type)) UNREACHABLE();
        return pkpy::make_shared<PyObject, Py_<RAW(T)>>(OBJ_GET(Type, type), _value);
    }
    template<typename T>
    inline PyVar new_object(const PyVar& type, T&& _value) {
        if(!type->is_type(tp_type)) UNREACHABLE();
        return pkpy::make_shared<PyObject, Py_<RAW(T)>>(OBJ_GET(Type, type), std::move(_value));
    }

    template<typename T>
    inline PyVar new_object(Type type, const T& _value) {
        return pkpy::make_shared<PyObject, Py_<RAW(T)>>(type, _value);
    }
    template<typename T>
    inline PyVar new_object(Type type, T&& _value) {
        return pkpy::make_shared<PyObject, Py_<RAW(T)>>(type, std::move(_value));
    }

    template<typename T, typename... Args>
    inline PyVar new_object(Args&&... args) {
        return new_object(T::_type(this), T(std::forward<Args>(args)...));
    }

    PyVar new_module(const Str& name) {
        PyVar obj = new_object(tp_module, DUMMY_VAL);
        setattr(obj, __name__, PyStr(name));
        _modules[name] = obj;
        return obj;
    }

    PyVarOrNull getattr(const PyVar& obj, const Str& name, bool throw_err=true) {
        pkpy::NameDict::iterator it;
        PyObject* cls;

        if(obj->is_type(tp_super)){
            const PyVar* root = &obj;
            int depth = 1;
            while(true){
                root = &OBJ_GET(PyVar, *root);
                if(!(*root)->is_type(tp_super)) break;
                depth++;
            }
            cls = _t(*root).get();
            for(int i=0; i<depth; i++) cls = cls->attr(__base__).get();

            it = (*root)->attr().find(name);
            if(it != (*root)->attr().end()) return it->second;
        }else{
            if(obj->is_attr_valid()){
                it = obj->attr().find(name);
                if(it != obj->attr().end()) return it->second;
            }
            cls = _t(obj).get();
        }

        while(cls != None.get()) {
            it = cls->attr().find(name);
            if(it != cls->attr().end()){
                PyVar valueFromCls = it->second;
                if(valueFromCls->is_type(tp_function) || valueFromCls->is_type(tp_native_function)){
                    return PyBoundMethod({obj, std::move(valueFromCls)});
                }else{
                    return valueFromCls;
                }
            }
            cls = cls->attr()[__base__].get();
        }
        if(throw_err) AttributeError(obj, name);
        return nullptr;
    }

    template<typename T>
    inline void setattr(PyVar& obj, const Str& name, T&& value) {
        PyObject* p = obj.get();
        while(p->is_type(tp_super)) p = static_cast<PyVar*>(p->value())->get();
        if(!p->is_attr_valid()) TypeError("cannot set attribute");
        p->attr()[name] = std::forward<T>(value);
    }

    template<int ARGC>
    void bind_method(PyVar obj, Str funcName, NativeFuncRaw fn) {
        check_type(obj, tp_type);
        setattr(obj, funcName, PyNativeFunc(pkpy::NativeFunc(fn, ARGC, true)));
    }

    template<int ARGC>
    void bind_func(PyVar obj, Str funcName, NativeFuncRaw fn) {
        setattr(obj, funcName, PyNativeFunc(pkpy::NativeFunc(fn, ARGC, false)));
    }

    template<int ARGC>
    void bind_func(Str typeName, Str funcName, NativeFuncRaw fn) {
        bind_func<ARGC>(_types[typeName], funcName, fn);
    }

    template<int ARGC>
    void bind_method(Str typeName, Str funcName, NativeFuncRaw fn) {
        bind_method<ARGC>(_types[typeName], funcName, fn);
    }

    template<int ARGC, typename... Args>
    void bind_static_method(Args&&... args) {
        bind_func<ARGC>(std::forward<Args>(args)...);
    }

    template<int ARGC>
    void _bind_methods(std::vector<Str> typeNames, Str funcName, NativeFuncRaw fn) {
        for(auto& typeName : typeNames) bind_method<ARGC>(typeName, funcName, fn);
    }

    template<int ARGC>
    void bind_builtin_func(Str funcName, NativeFuncRaw fn) {
        bind_func<ARGC>(builtins, funcName, fn);
    }

    inline f64 num_to_float(const PyVar& obj){
        if (obj->is_type(tp_int)){
            return (f64)PyInt_AS_C(obj);
        }else if(obj->is_type(tp_float)){
            return PyFloat_AS_C(obj);
        }
        TypeError("expected 'int' or 'float', got " + OBJ_NAME(_t(obj)).escape(true));
        return 0;
    }

    PyVar num_negated(const PyVar& obj){
        if (obj->is_type(tp_int)){
            return PyInt(-PyInt_AS_C(obj));
        }else if(obj->is_type(tp_float)){
            return PyFloat(-PyFloat_AS_C(obj));
        }
        TypeError("unsupported operand type(s) for -");
        return nullptr;
    }

    int normalized_index(int index, int size){
        if(index < 0) index += size;
        if(index < 0 || index >= size){
            IndexError(std::to_string(index) + " not in [0, " + std::to_string(size) + ")");
        }
        return index;
    }

    Str disassemble(CodeObject_ co){
        std::vector<int> jumpTargets;
        for(auto byte : co->codes){
            if(byte.op == OP_JUMP_ABSOLUTE || byte.op == OP_SAFE_JUMP_ABSOLUTE || byte.op == OP_POP_JUMP_IF_FALSE){
                jumpTargets.push_back(byte.arg);
            }
        }
        StrStream ss;
        ss << std::string(54, '-') << '\n';
        ss << co->name << ":\n";
        int prev_line = -1;
        for(int i=0; i<co->codes.size(); i++){
            const Bytecode& byte = co->codes[i];
            Str line = std::to_string(byte.line);
            if(byte.line == prev_line) line = "";
            else{
                if(prev_line != -1) ss << "\n";
                prev_line = byte.line;
            }

            std::string pointer;
            if(std::find(jumpTargets.begin(), jumpTargets.end(), i) != jumpTargets.end()){
                pointer = "-> ";
            }else{
                pointer = "   ";
            }
            ss << pad(line, 8) << pointer << pad(std::to_string(i), 3);
            ss << " " << pad(OP_NAMES[byte.op], 20) << " ";
            // ss << pad(byte.arg == -1 ? "" : std::to_string(byte.arg), 5);
            std::string argStr = byte.arg == -1 ? "" : std::to_string(byte.arg);
            if(byte.op == OP_LOAD_CONST){
                argStr += " (" + PyStr_AS_C(asRepr(co->consts[byte.arg])) + ")";
            }
            if(byte.op == OP_LOAD_NAME_REF || byte.op == OP_LOAD_NAME || byte.op == OP_RAISE){
                argStr += " (" + co->names[byte.arg].first.escape(true) + ")";
            }
            ss << pad(argStr, 20);      // may overflow
            ss << co->blocks[byte.block].to_string();
            if(i != co->codes.size() - 1) ss << '\n';
        }
        StrStream consts;
        consts << "co_consts: ";
        consts << PyStr_AS_C(asRepr(PyList(co->consts)));

        StrStream names;
        names << "co_names: ";
        pkpy::List list;
        for(int i=0; i<co->names.size(); i++){
            list.push_back(PyStr(co->names[i].first));
        }
        names << PyStr_AS_C(asRepr(PyList(list)));
        ss << '\n' << consts.str() << '\n' << names.str() << '\n';

        for(int i=0; i<co->consts.size(); i++){
            PyVar obj = co->consts[i];
            if(obj->is_type(tp_function)){
                const auto& f = PyFunction_AS_C(obj);
                ss << disassemble(f->code);
            }
        }
        return Str(ss.str());
    }

    // for quick access
    Type tp_object, tp_type, tp_int, tp_float, tp_bool, tp_str;
    Type tp_list, tp_tuple;
    Type tp_function, tp_native_function, tp_native_iterator, tp_bound_method;
    Type tp_slice, tp_range, tp_module, tp_ref;
    Type tp_super, tp_exception;

    template<typename P>
    inline PyVarRef PyRef(P&& value) {
        static_assert(std::is_base_of<BaseRef, std::remove_reference_t<P>>::value, "P should derive from BaseRef");
        return new_object(tp_ref, std::forward<P>(value));
    }

    inline const BaseRef* PyRef_AS_C(const PyVar& obj)
    {
        if(!obj->is_type(tp_ref)) TypeError("expected an l-value");
        return (const BaseRef*)(obj->value());
    }

    inline const Str& PyStr_AS_C(const PyVar& obj) {
        check_type(obj, tp_str);
        return OBJ_GET(Str, obj);
    }
    inline PyVar PyStr(const Str& value) {
        // some BUGs here
        // if(value.size() == 1){
        //     char c = value.c_str()[0];
        //     if(c >= 0) return _ascii_str_pool[(int)c];
        // }
        return new_object(tp_str, value);
    }

    DEF_NATIVE(Int, i64, tp_int)
    DEF_NATIVE(Float, f64, tp_float)
    DEF_NATIVE(List, pkpy::List, tp_list)
    DEF_NATIVE(Tuple, pkpy::Tuple, tp_tuple)
    DEF_NATIVE(Function, pkpy::Function_, tp_function)
    DEF_NATIVE(NativeFunc, pkpy::NativeFunc, tp_native_function)
    DEF_NATIVE(Iter, pkpy::shared_ptr<BaseIter>, tp_native_iterator)
    DEF_NATIVE(BoundMethod, pkpy::BoundMethod, tp_bound_method)
    DEF_NATIVE(Range, pkpy::Range, tp_range)
    DEF_NATIVE(Slice, pkpy::Slice, tp_slice)
    DEF_NATIVE(Exception, pkpy::Exception, tp_exception)

    // there is only one True/False, so no need to copy them!
    inline bool PyBool_AS_C(const PyVar& obj){return obj == True;}
    inline const PyVar& PyBool(bool value){return value ? True : False;}

    void init_builtin_types(){
        PyVar _tp_object = pkpy::make_shared<PyObject, Py_<Type>>(1, 0);
        PyVar _tp_type = pkpy::make_shared<PyObject, Py_<Type>>(1, 1);
        _all_types.push_back(_tp_object);
        _all_types.push_back(_tp_type);
        tp_object = 0; tp_type = 1;

        _types["object"] = _tp_object;
        _types["type"] = _tp_type;

        tp_bool = _new_type_object("bool");
        tp_int = _new_type_object("int");
        tp_float = _new_type_object("float");
        tp_str = _new_type_object("str");
        tp_list = _new_type_object("list");
        tp_tuple = _new_type_object("tuple");
        tp_slice = _new_type_object("slice");
        tp_range = _new_type_object("range");
        tp_module = _new_type_object("module");
        tp_ref = _new_type_object("_ref");

        tp_function = _new_type_object("function");
        tp_native_function = _new_type_object("native_function");
        tp_native_iterator = _new_type_object("native_iterator");
        tp_bound_method = _new_type_object("bound_method");
        tp_super = _new_type_object("super");
        tp_exception = _new_type_object("Exception");

        this->None = new_object(_new_type_object("NoneType"), DUMMY_VAL);
        this->Ellipsis = new_object(_new_type_object("ellipsis"), DUMMY_VAL);
        this->True = new_object(tp_bool, true);
        this->False = new_object(tp_bool, false);
        this->builtins = new_module("builtins");
        this->_main = new_module("__main__");
        this->_py_op_call = new_object(_new_type_object("_internal"), DUMMY_VAL);
        this->_py_op_yield = new_object(_new_type_object("_internal"), DUMMY_VAL);

        setattr(_t(tp_type), __base__, _t(tp_object));
        setattr(_t(tp_object), __base__, None);

        for (auto& [name, type] : _types) {
            setattr(type, __name__, PyStr(name));
        }

        std::vector<Str> publicTypes = {"type", "object", "bool", "int", "float", "str", "list", "tuple", "range"};
        for (auto& name : publicTypes) {
            setattr(builtins, name, _types[name]);
        }
    }

    i64 hash(const PyVar& obj){
        if (obj->is_type(tp_int)) return PyInt_AS_C(obj);
        if (obj->is_type(tp_bool)) return PyBool_AS_C(obj) ? 1 : 0;
        if (obj->is_type(tp_float)){
            f64 val = PyFloat_AS_C(obj);
            return (i64)std::hash<f64>()(val);
        }
        if (obj->is_type(tp_str)) return PyStr_AS_C(obj).hash();
        if (obj->is_type(tp_type)) return (i64)obj.get();
        if (obj->is_type(tp_tuple)) {
            i64 x = 1000003;
            const pkpy::Tuple& items = PyTuple_AS_C(obj);
            for (int i=0; i<items.size(); i++) {
                i64 y = hash(items[i]);
                x = x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2)); // recommended by Github Copilot
            }
            return x;
        }
        TypeError("unhashable type: " +  OBJ_NAME(_t(obj)).escape(true));
        return 0;
    }

    /***** Error Reporter *****/
private:
    void _error(const Str& name, const Str& msg){
        _error(pkpy::Exception(name, msg));
    }

    void _error(pkpy::Exception e){
        if(callstack.empty()){
            e.is_re = false;
            throw e;
        }
        top_frame()->push(PyException(e));
        _raise();
    }

    void _raise(){
        bool ok = top_frame()->jump_to_exception_handler();
        if(ok) throw HandledException();
        else throw UnhandledException();
    }

public:
    void IOError(const Str& msg) { _error("IOError", msg); }
    void NotImplementedError(){ _error("NotImplementedError", ""); }
    void TypeError(const Str& msg){ _error("TypeError", msg); }
    void ZeroDivisionError(){ _error("ZeroDivisionError", "division by zero"); }
    void IndexError(const Str& msg){ _error("IndexError", msg); }
    void ValueError(const Str& msg){ _error("ValueError", msg); }
    void NameError(const Str& name){ _error("NameError", "name " + name.escape(true) + " is not defined"); }

    void AttributeError(PyVar obj, const Str& name){
        _error("AttributeError", "type " +  OBJ_NAME(_t(obj)).escape(true) + " has no attribute " + name.escape(true));
    }

    inline void check_type(const PyVar& obj, Type type){
        if(obj->is_type(type)) return;
        TypeError("expected " + OBJ_NAME(_t(type)).escape(true) + ", but got " + OBJ_NAME(_t(obj)).escape(true));
    }

    inline PyVar& _t(Type t){
        return _all_types[t.index];
    }

    inline PyVar& _t(const PyVar& obj){
        return _all_types[OBJ_GET(Type, _t(obj->type)).index];
    }

    template<typename T>
    PyVar register_class(PyVar mod){
        PyVar type = new_type_object(mod, T::_name(), _t(tp_object));
        if(OBJ_NAME(mod) != T::_mod()) UNREACHABLE();
        T::_register(this, mod, type);
        return type;
    }

    template<typename T>
    inline T& py_cast(const PyVar& obj){
        check_type(obj, T::_type(this));
        return OBJ_GET(T, obj);
    }

    ~VM() {
        if(!use_stdio){
            delete _stdout;
            delete _stderr;
        }
    }

    CodeObject_ compile(Str source, Str filename, CompileMode mode);
};

/***** Pointers' Impl *****/
PyVar NameRef::get(VM* vm, Frame* frame) const{
    PyVar* val;
    val = frame->f_locals().try_get(name());
    if(val) return *val;
    val = frame->f_globals().try_get(name());
    if(val) return *val;
    val = vm->builtins->attr().try_get(name());
    if(val) return *val;
    vm->NameError(name());
    return nullptr;
}

void NameRef::set(VM* vm, Frame* frame, PyVar val) const{
    switch(scope()) {
        case NAME_LOCAL: frame->f_locals()[name()] = std::move(val); break;
        case NAME_GLOBAL:
        {
            PyVar* existing = frame->f_locals().try_get(name());
            if(existing != nullptr){
                *existing = std::move(val);
            }else{
                frame->f_globals()[name()] = std::move(val);
            }
        } break;
        default: UNREACHABLE();
    }
}

void NameRef::del(VM* vm, Frame* frame) const{
    switch(scope()) {
        case NAME_LOCAL: {
            if(frame->f_locals().contains(name())){
                frame->f_locals().erase(name());
            }else{
                vm->NameError(name());
            }
        } break;
        case NAME_GLOBAL:
        {
            if(frame->f_locals().contains(name())){
                frame->f_locals().erase(name());
            }else{
                if(frame->f_globals().contains(name())){
                    frame->f_globals().erase(name());
                }else{
                    vm->NameError(name());
                }
            }
        } break;
        default: UNREACHABLE();
    }
}

PyVar AttrRef::get(VM* vm, Frame* frame) const{
    return vm->getattr(obj, attr.name());
}

void AttrRef::set(VM* vm, Frame* frame, PyVar val) const{
    vm->setattr(obj, attr.name(), val);
}

void AttrRef::del(VM* vm, Frame* frame) const{
    if(!obj->is_attr_valid()) vm->TypeError("cannot delete attribute");
    if(!obj->attr().contains(attr.name())) vm->AttributeError(obj, attr.name());
    obj->attr().erase(attr.name());
}

PyVar IndexRef::get(VM* vm, Frame* frame) const{
    return vm->call(obj, __getitem__, pkpy::one_arg(index));
}

void IndexRef::set(VM* vm, Frame* frame, PyVar val) const{
    vm->call(obj, __setitem__, pkpy::two_args(index, val));
}

void IndexRef::del(VM* vm, Frame* frame) const{
    vm->call(obj, __delitem__, pkpy::one_arg(index));
}

PyVar TupleRef::get(VM* vm, Frame* frame) const{
    pkpy::Tuple args(objs.size());
    for (int i = 0; i < objs.size(); i++) {
        args[i] = vm->PyRef_AS_C(objs[i])->get(vm, frame);
    }
    return vm->PyTuple(std::move(args));
}

void TupleRef::set(VM* vm, Frame* frame, PyVar val) const{
#define TUPLE_REF_SET() \
    if(args.size() > objs.size()) vm->ValueError("too many values to unpack");       \
    if(args.size() < objs.size()) vm->ValueError("not enough values to unpack");     \
    for (int i = 0; i < objs.size(); i++) vm->PyRef_AS_C(objs[i])->set(vm, frame, args[i]);

    if(val->is_type(vm->tp_tuple)){
        const pkpy::Tuple& args = OBJ_GET(pkpy::Tuple, val);
        TUPLE_REF_SET()
    }else if(val->is_type(vm->tp_list)){
        const pkpy::List& args = OBJ_GET(pkpy::List, val);
        TUPLE_REF_SET()
    }else{
        vm->TypeError("only tuple or list can be unpacked");
    }
#undef TUPLE_REF_SET
}

void TupleRef::del(VM* vm, Frame* frame) const{
    for(int i=0; i<objs.size(); i++) vm->PyRef_AS_C(objs[i])->del(vm, frame);
}

/***** Frame's Impl *****/
inline void Frame::try_deref(VM* vm, PyVar& v){
    if(v->is_type(vm->tp_ref)) v = vm->PyRef_AS_C(v)->get(vm, this);
}

PyVar pkpy::NativeFunc::operator()(VM* vm, pkpy::Args& args) const{
    int args_size = args.size() - (int)method;  // remove self
    if(argc != -1 && args_size != argc) {
        vm->TypeError("expected " + std::to_string(argc) + " arguments, but got " + std::to_string(args_size));
    }
    return f(vm, args);
}

void CodeObject::optimize(VM* vm){
    for(int i=1; i<codes.size(); i++){
        if(codes[i].op == OP_UNARY_NEGATIVE && codes[i-1].op == OP_LOAD_CONST){
            codes[i].op = OP_NO_OP;
            int pos = codes[i-1].arg;
            consts[pos] = vm->num_negated(consts[pos]);
        }
    }
}