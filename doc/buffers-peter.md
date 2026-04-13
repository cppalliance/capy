Peter's position on buffer sequence design in Capy:

---

We need to clarify our approach for manipulating slices (byte ranges) of buffer sequences. We can use the implementation of read as a canonical example of this need.

Our current implementation is

    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming);
        consuming.consume(n);
        total_read += n;
        if(ec)
            co_return {ec, total_read};
    }

    co_return {{}, total_read};

which is actually a legitimate approach to things - have a stateful slice type (not necessarily called consuming_buffers), construct it over the passed buffer sequence, then iterate by alternately passing it to read_some and removing a prefix.

One change we can make to the above (besides the name of the slice class) would be to not require it to be a buffer sequence itself. Instead of co_await read_some(consuming), we can have co_await read_some(consuming.data()), with the buffer sequence only produced on demand.

In earlier discussions, however, I was told that the above is a temporary implementation subject to be replaced with the real one, the real one being something along the lines of

    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    auto seq = sans_prefix(buffers, 0);

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(seq);
        seq = remove_prefix(seq, n);
        total_read += n;
        if(ec)
            co_return {ec, total_read};
    }

    co_return {{}, total_read};

The implication here is that sans_prefix(buffers, 0) would be required to return a buffer sequence seq such that the result of calling remove_prefix on it would be assignable to seq.

While that's possible to implement, I don't like it one bit; the specifications of sans_prefix and remove_prefix become entangled with special cases, and having the "begin" operation be spelled sans_prefix(buffers, 0) is kind of stupid.

Essentially, this implements our current approach, but spells the operations and the iteration state type in a weird manner. I would prefer the approach of naming the iteration state type and its operations explicitly.

There's one alternative we haven't considered, though, and towards which I have gravitated. We can also eliminate the need for a slice-producing type by pushing the responsibility of handling slices onto the implementers of read_some and write_some.

That is, we can add offset and length parameters to read_some, with the result being

    std::size_t const total_size = buffer_size(buffers);

    std::size_t offset = 0, length = total_size;

    while(offset < total_size)
    {
        auto [ec, n] = co_await stream.read_some(buffers, offset, length);
        offset += n;
        length -= n;
        if(ec)
            co_return {ec, offset};
    }

    co_return {{}, offset};

This doesn't increase the total implementation complexity compared to the user passing a slice type to read_some, because we can still provide the slice type, and the implementer of read_some can still implement the offset+length function in terms of the existing offsetless/lengthless function by passing it a slice. However, in many cases the implementation of read_some can take advantage of offset and length natively, without much additional complexity.

---

At the moment, read_some is specified as

template<MutableBufferSequence Buffers>
IoAwaitable auto read_some(Buffers buffers);
That is, it takes buffers by value.

This doesn't seem correct. It would imply that std::vector<mutable_buffer> is required to be copied by read_some, which is unnecessary.

And if we change this to e.g. Buffers&& buffers, we need to clarify that read_some doesn't mutate buffers in the lvalue case, so that the caller can still use buffers in a subsequent read_some (or write_some) call.

The rvalue case is trickier, but we need to have clarity on #261 before deciding.

---

We should remove all tag_invoke customization points pertaining to buffer sequences, and all custom buffer sequence types that only exist because they customize an operation. Buffer sequences should be generic ranges of const_bufffer or mutable_buffer (or more precisely, any generic range should be accepted as a buffer sequence; const_buffer and mutable_buffer by itself would still be able to serve as buffer sequences.)

---

It will be convenient to have a read_at_least algorithm that is a straightforward extension of read. While read reads exactly buffer_size(buffers) bytes, read_at_least would take the minimum amount of bytes as a parameter, and the only change would be in the loop condition. Instead of while(bytes_read < buffer_size(buffers)), it would be while(bytes_read < bytes_requested).

One motivating example can be found here:

https://github.com/pdimov/corosio_protocol_bench/blob/ea373f3f9e3c1945627c85f24fb9c256128bb11a/buffered_socket_source.hpp#L62

The "buffered source" implementation needs to read the n bytes requested by the user, and to fill its buffer, with a single invocation. While n is a required amount and must be met or exceeded, the subsequent N bytes filling the buffer are optional and there's no need to block or loop for them.

I don't have a motivating example for write_at_least, but we should provide it for consistency and symmetry.

The one subtlety here is that it's possible for the user to pass parameters that are impossible to satisfy (if the requested minimum amount of bytes exceeds buffer_size(buffers). In this case, I believe that the function should fail immediately, with {EINVAL, 0}.

---
