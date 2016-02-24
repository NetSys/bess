## Packet Batch

Batched packet processing is a well-known technique to amortize per-packet overhead, and BESS also extensively uses packet batches. In the BESS datapath pipeline, [packet buffers](snbuf.md) are passed across modules in a batch (`struct pkt_batch`), rather than as individual buffers. A module processes all packets in the batch, before passing it to the next module. Also, task scheduling and packet buffer (de)allocation are done with a packet batch.

`core/pktbatch.h` defines a simple data structure to represent a packet batch as follows:

```c
#define MAX_PKT_BURST			32

struct pkt_batch {
	int cnt;
	struct snbuf * restrict pkts[MAX_PKT_BURST];
};
```

Essentially, `struct pkt_batch` is an array of [packet buffer](snbuf.md) pointers (`pkts`) and a counter (`cnt`) that indicates how many packets this packet batch has (from `batch->pkts[0]` to `batch->pkts[cnt-1]`). The maximum number of packets in a batch is 32; if a system is overloaded (thus queues at ports are likely to build up), a packet batch received from a port will almost always have 32 packets.

> NOTE: Always use the `MAX_PKT_BURST` macro in your code, not the constant number 32.


### Allocating a new packet batch

You do not need to dynamically allocate a packet batch (will hurt performance). Instead, you can create it as a stack (auto) variable and initialize with `batch_clear()`, or manually set the `cnt` field. The following example illustrates how to "duplicate" a packet batch, without dynamic memory (de)allocation. The following code snippet shows how it creates a batch of newly allocated packet buffers and pass it to the next module. The example is a simplified version of `core/modules/source.c`.

```c
struct task_result generator_example(struct module *m, void *arg)
{
    struct pkt_batch batch;     /* allocated in the stack */
    const int pkt_size = 1024;
    
    /* it fills the pointers of allocated packet buffers to batch.pkts,
     * and returns the number of allocated packets  */
    batch.cnt = snb_alloc_bulk(batch.pkts, MAX_PKT_BATCH, 1024);
    
    if (batch.cnt > 0)
        run_next_module(m, &batch);
        
    ...
}
```

> NOTE: Since packet batches are typically stack variables (as shown above), the memory address of a packet batch must not be stored for later use. Instead, create a separate buffer and copy the pointer array of the packet batch.


### Splitting a packet batch into multiple batches

It is often necessary for a module to split a packet batch into multiple ones. For instance, an IP forwarding module should send packets to downstream modules, depending on the lookup results (next hops). The following code example shows a hypothetical module, which send pkts[0], pkts[2], pkts[4], ... to the output gate 0 and pkts[1], pkts[3], pkts[5], ... to the output gate 1.

```c
static void process_batch(struct module *m, struct pkt_batch *batch)
{
    /* allocated in the stack */
    struct pkt_batch even;     
    struct pkt_batch odd;
    
    /* initialize */
    batch_clear(&even);        
    batch_clear(&odd);
    
    for (int i = 0; i < batch->cnt; i++) {
        if (i % 2 == 0)
            batch_add(&even, batch->pkts[i]);
        else
            batch_add(&odd, batch->pkts[i]);
    }
        
    run_choose_module(m, 0, &even);
    run_choose_module(m, 1, &odd);
}
```

> NOTE: Alternatively, you can use the [`run_split()`](writing_modules.md) helper function.

### Assumptions and requirements

Modules and drivers are expected to follow these assumptions:

* A packet batch with zero packets (`cnt == 0`) is considered invalid, and should not be passed to other modules or BESS core API.
* A single packet batch cannot have more than `MAX_PKT_BURST` packets.
* The ordering of packet buffers within a packet batch is meaningful, so should not be reordered unnecessarily.
  * Packets appearing early in the array are *older*, i.e., received earlier.
  * When passed to a port, the first packet in a batch will be transmitted first.
