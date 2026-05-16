# cactor

`cactor` is a small C++ actor-runtime experiment built in the style used across this repository family: plain C++ with a C-style API, explicit memory ownership, and a bias toward simple runtime primitives.

The current codebase implements the core of an actor scheduler around:

- fixed actor storage owned by a `system_t`
- a per-actor mailbox
- a global blocking worker queue of runnable actors
- pointer-based messages with sender/recipient metadata
- optional lock-free message pooling for reuse

This repository is still in progress. The mailbox and worker-queue path is the active design; some older queue abstractions remain in the tree but are not the main execution path.

## What the runtime does

At a high level, the runtime schedules actors, not individual messages.

When one actor sends a message to another:

1. the sender fills the message envelope with sender and recipient pointers
2. the message is pushed into the recipient mailbox
3. if the mailbox was previously idle, the recipient actor is pushed once onto the global worker queue

Worker threads pop actors from that queue, rotate the actor's mailbox into a private batch, process each message in that batch, and then decide whether the actor must be re-enqueued.

This avoids queueing the same actor multiple times during a burst of messages while still preserving FIFO order inside each isolated batch.

## Public API

The public header is [source/main/include/cactor/c_actor.h](source/main/include/cactor/c_actor.h).

The current API surface is intentionally small:

- `create_system(...)`
- `destroy_system(...)`
- `actor_join(...)`
- `actor_leave(...)`
- `actor_send(...)`

The callback types expose two phases:

- `actor_process_fn`
- `actor_returned_fn`

In the current implementation, the worker loop actively uses `actor_process_fn` and `actor_returned_fn`. 

## Messages and ownership

Messages are passed by pointer. The runtime does not copy payloads.

Each `msg_t` contains:

- sender actor pointer
- recipient actor pointer
- message id
- payload pointer
- link pointer used internally by mailbox and pool structures

This design keeps the runtime lean and leaves payload layout entirely under user control. It also encourages sender-owned message lifetime: a sender can allocate or recycle a message, send it, let the recipient process it, and then reclaim it through the return callback.

There is already a lock-free `msg_pool_t` implementation in the repository for this reuse pattern.

## Internal design

The active runtime path is built from the following pieces:

- `actor_t`: actor metadata, callbacks, user pointer, and scheduler linkage
- `mailbox_t`: per-actor inbound queue plus scheduling state machine
- `worker_queue_t`: blocking queue of runnable actors for worker threads
- `msg_pool_t`: lock-free free-list for preallocated messages

The mailbox is the most important primitive.

It uses:

- an atomic inbound message stack for lock-free multi-producer publication
- a small state machine to avoid duplicate actor scheduling

The worker side snapshots that inbound stack, reverses it into FIFO order, processes the batch privately, and then finalizes the mailbox state. If more messages arrived during processing, the actor is re-enqueued exactly once.

More detail is documented in [docs/design.md](docs/design.md).

## Repository layout

Typical directories in this repository:

- [source/main/include/cactor](source/main/include/cactor): public headers
- [source/main/include/cactor/private](source/main/include/cactor/private): internal headers
- [source/main/cpp](source/main/cpp): runtime implementation
- [source/test/cpp](source/test/cpp): unit tests and experiments
- [docs](docs): design notes
- [package](package): package metadata used by the repository's code-generation/build tooling
- [target](target): generated build files and outputs

## Build and generation flow

This repository uses the same generated-project workflow as the surrounding `jurgen-kluft` packages.

The entry point is [cactor.go](cactor.go), which uses `ccode` to generate project/build files from [package/package.go](package/package.go).

Typical workflow:

1. run the generator entry point to produce build files for this package
2. build the generated project using the chosen backend, typically `clay`

Package dependencies declared in [package/package.go](package/package.go):

- `cbase`
- `cunittest`

## Current status

The repository already contains the key concurrency building blocks, but it should be treated as an in-progress runtime rather than a finished actor framework.

Visible state from the current source:

- the mailbox and worker queue implementations are present and form the active scheduling path
- the message pool implementation exists and has a basic unit test
- the actor test surface is still mostly skeletal
- some lifecycle functions visible in declarations appear incomplete in the current source snapshot
- `create_system(...)` still accepts `max_messages` and `max_producers`, but the active mailbox-based path does not currently consume those values

That means the repository is already useful as a design and implementation base for a low-overhead actor runtime, but not yet packaged as a fully finished, stable library.

## Example shape

The intended usage pattern looks like this:

```cpp
using namespace ncore::nactor;

system_t* system = create_system(allocator, num_threads, max_actors, max_messages, max_producers);

actor_t* a = actor_join(system, user_a, on_received_a, on_process_a, on_returned_a);
actor_t* b = actor_join(system, user_b, on_received_b, on_process_b, on_returned_b);

msg_t* msg = /* allocate or reuse from a pool */;
msg->m_id = /* message id */;
msg->m_message = /* payload */;

actor_send(a, msg, b);

destroy_system(system);
```

Treat this as an API sketch rather than a full tutorial. The current tree does not yet contain a complete end-to-end sample application, and the actor lifecycle implementation is still being filled in.

## Related documentation

- [docs/design.md](docs/design.md): design analysis of the current runtime implementation



