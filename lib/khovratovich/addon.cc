/*********************************************************************
 * Equihash addon for Node.js
 *
 * Copyright (c) 2017 Digital Bazaar, Inc.
 *
 * MIT License
 * <https://github.com/digitalbazaar/equihash/blob/master/LICENSE>
 ********************************************************************/

#include <arpa/inet.h>
#include <nan.h>
//#include "addon.h"   // NOLINT(build/include)
#include "pow.h"  // NOLINT(build/include)

using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::GetFunction;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::Set;
using Nan::To;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

class EquihashSolveWorker : public AsyncWorker {
public:
    EquihashSolveWorker(const unsigned n, const unsigned k, Seed seed, Callback *callback)
        : AsyncWorker(callback), n(n), k(k), seed(seed) {}
    ~EquihashSolveWorker() {}

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute () {
        Equihash equihash(n, k, seed);
        Proof p = equihash.FindProof();
        solution = p.inputs;
        nonce = p.nonce;
        //printhex("solution", &solution[0], solution.size());
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback () {
        HandleScope scope;
        Local<Object> obj = New<Object>();

        // to big-endian order
        std::vector<Input> beInputs(solution.size());
        for(size_t i = 0; i < solution.size(); i++) {
            beInputs[i] = htonl(solution[i]);
        }

        Local<Object> proofValue =
            Nan::CopyBuffer((const char*)&beInputs[0], beInputs.size() * 4)
            .ToLocalChecked();

        //printhex("solution COPY", &solution[0], solution.size());

        obj->Set(New("n").ToLocalChecked(), New(n));
        obj->Set(New("k").ToLocalChecked(), New(k));
        obj->Set(New("nonce").ToLocalChecked(), New(nonce));
        obj->Set(New("value").ToLocalChecked(), proofValue);

        Local<Value> argv[] = {
            Null(),
            obj
        };

        callback->Call(2, argv);
    }

private:
    unsigned n;
    unsigned k;
    Nonce nonce;
    Seed seed;
    std::vector<Input> solution;
};

class EquihashVerifyWorker : public AsyncWorker {
public:
    EquihashVerifyWorker(Proof proof, Callback *callback)
        : AsyncWorker(callback), proof(proof) {}
    ~EquihashVerifyWorker() {}

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute () {
        verified = proof.Test();
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback () {
        HandleScope scope;

        Local<Value> argv[] = {
            Null(),
            New(verified)
        };

        callback->Call(2, argv);
    }

private:
    Proof proof;
    bool verified;
};

NAN_METHOD(Solve) {
    // ensure first argument is an object
    if(!info[0]->IsObject()) {
        Nan::ThrowTypeError("'options' must be an object");
        return;
    }
    // ensure second argument is a callback
    if(!info[1]->IsFunction()) {
        Nan::ThrowTypeError("'callback' must be a function");
        return;
    }

    Callback *callback = new Callback(info[1].As<Function>());
    Handle<Object> object = Handle<Object>::Cast(info[0]);
    Handle<Value> nValue = object->Get(New("n").ToLocalChecked());
    Handle<Value> kValue = object->Get(New("k").ToLocalChecked());
    Handle<Value> seedValue = object->Get(New("seed").ToLocalChecked());

    const unsigned n = To<uint32_t>(nValue).FromJust();
    const unsigned k = To<uint32_t>(kValue).FromJust();
    size_t bufferLength = node::Buffer::Length(seedValue) / 4;
    unsigned* seedBuffer = (unsigned*)node::Buffer::Data(seedValue);

    //printhex("seed", seedBuffer, bufferLength);

    Seed seed(seedBuffer, bufferLength);

    AsyncQueueWorker(new EquihashSolveWorker(n, k, seed, callback));
}

NAN_METHOD(Verify) {
    // ensure first argument is an object
    if(!info[0]->IsObject()) {
        Nan::ThrowTypeError("'options' must be an object");
        return;
    }

    Callback *callback = new Callback(info[1].As<Function>());
    // unbundle all data needed to check the proof
    Handle<Object> object = Handle<Object>::Cast(info[0]);
    Handle<Value> nValue = object->Get(New("n").ToLocalChecked());
    Handle<Value> kValue = object->Get(New("k").ToLocalChecked());
    Handle<Value> nonceValue = object->Get(New("nonce").ToLocalChecked());
    Handle<Value> seedValue = object->Get(New("seed").ToLocalChecked());
    Handle<Value> inputValue = object->Get(New("value").ToLocalChecked());

    const unsigned n = To<uint32_t>(nValue).FromJust();
    const unsigned k = To<uint32_t>(kValue).FromJust();
    const unsigned nonce = To<uint32_t>(nonceValue).FromJust();
    size_t seedBufferLength = node::Buffer::Length(seedValue) / 4;
    unsigned* seedBuffer = (unsigned*)node::Buffer::Data(seedValue);
    size_t inputBufferLength = node::Buffer::Length(inputValue) / 4;
    unsigned* inputBuffer = (unsigned*)node::Buffer::Data(inputValue);

    //printhex("seed", seedBuffer, seedBufferLength);
    //printhex("input", inputBuffer, inputBufferLength);

    // initialize the proof object
    Seed seed(seedBuffer, seedBufferLength);
    std::vector<Input> inputs(inputBufferLength);
    // to big-endian order
    for(size_t i = 0; i < inputs.size(); i++) {
        inputs[i] = ntohl(inputBuffer[i]);
    }
    Proof p(n, k, seed, nonce, inputs);

    AsyncQueueWorker(new EquihashVerifyWorker(p, callback));
}

NAN_MODULE_INIT(InitAll) {
    Set(target, New<String>("solve").ToLocalChecked(),
            GetFunction(New<FunctionTemplate>(Solve)).ToLocalChecked());
    Set(target, New<String>("verify").ToLocalChecked(),
            GetFunction(New<FunctionTemplate>(Verify)).ToLocalChecked());
}

NODE_MODULE(addon, InitAll)
