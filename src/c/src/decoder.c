#include <unistd.h>
#include <stdlib.h>

#include "huffman/decoder.h"
#include "huffman/malloc.h"
#include "huffman/sys.h"


static huf_error_t
__huf_decode_partial(huf_decoder_t *self, const uint8_t *buf, size_t len)
{
    __try__;

    huf_error_t err;

    uint8_t bit_rwbuf;
    uint8_t bit_offset;

    size_t position;

    __argument__(self);
    __argument__(buf);

    if (self->last_node == 0) {
        self->last_node = self->huffman_tree->root;
    }

    for (position = 0; position < len; position++) {
        bit_rwbuf = buf[position];

        for (bit_offset = 8; bit_offset > 0; bit_offset--) {
            if ((bit_rwbuf >> (bit_offset - 1)) & 1) {
                self->last_node = self->last_node->right;
            } else {
                self->last_node = self->last_node->left;
            }

            if (self->last_node->left || self->last_node->right) {
                continue;
            }

            err = huf_bufio_write_uint8(self->bufio_read_writer, self->last_node->index);
            __assert__(err);

            self->last_node = self->huffman_tree->root;
        }
    }

    __finally__;
    __end__;
}


huf_error_t
huf_decoder_init(huf_decoder_t **self, huf_read_writer_t *read_writer)
{
    __try__;

    huf_error_t err;
    huf_decoder_t *self_ptr;

    __argument__(self);
    __argument__(read_writer);

    err = huf_malloc((void**) &self_ptr, sizeof(huf_decoder_t), 1);
    __assert__(err);

    *self = self_ptr;

    // Allocate memory for Huffman tree.
    err = huf_tree_init(&self_ptr->huffman_tree);
    __assert__(err);

    // Create buffered read-writer instance with 64KiB buffer.
    err = huf_bufio_read_writer_init(&self_ptr->bufio_read_writer, read_writer, __HUFFMAN_DEFAULT_BUFFER);
    __assert__(err);

    __finally__;
    __end__;
}


huf_error_t
huf_decoder_free(huf_decoder_t **self)
{
    __try__;

    huf_error_t err;
    huf_decoder_t *self_ptr;

    __argument__(self);

    self_ptr = *self;

    err = huf_tree_free(&self_ptr->huffman_tree);
    __assert__(err);

    err = huf_bufio_read_writer_free(&self_ptr->bufio_read_writer);
    __assert__(err);

    free(self_ptr);

    *self = NULL;

    __finally__;
    __end__;
}


huf_error_t
huf_decode(huf_reader_t reader, huf_writer_t writer, uint64_t len)
{
    __try__;

    huf_read_writer_t read_writer = {reader, writer, len};
    huf_decoder_t *self = NULL;

    huf_error_t err;

    uint8_t buf[__HUFFMAN_DEFAULT_BUFFER];
    uint64_t reader_length = 0;

    int16_t *tree_head = NULL;
    int16_t tree_length = 0;

    size_t left_to_read = len;
    size_t need_to_read;

    // Create decoder instance.
    err = huf_decoder_init(&self, &read_writer);
    __assert__(err);

    __debug__("SELF 1 %p\n", (void*)self);

    // Read the length of the original file.
    need_to_read = sizeof(reader_length);
    left_to_read -= need_to_read;
    err = huf_read(reader, &reader_length, &need_to_read);
    __assert__(err);

    __debug__("SELF 2 %p %lld\n", (void*)self, reader_length);

    // Read the length of the huffman tree.
    need_to_read = sizeof(tree_length);
    left_to_read -= need_to_read;
    err = huf_read(reader, &tree_length, &need_to_read);
    __assert__(err);

    __debug__("SELF 3 %p\n", (void*)self);

    // Allocate memory for huffman tree.
    err = huf_malloc((void**) &tree_head, sizeof(*tree_head), tree_length);
    __assert__(err);

    // Read flat huffman tree.
    need_to_read = tree_length * sizeof(*tree_head);
    left_to_read -= need_to_read;
    err = huf_read(reader, tree_head, &need_to_read);
    __assert__(err);

    __debug__("SELF 4 %p\n", (void*)self);

    // Create linked tree strcuture.
    err = huf_tree_deserialize(self->huffman_tree, tree_head, tree_length);
    __assert__(err);

    __debug__("SELF 5 %p\n", (void*)self);

    __debug__("ROOT %p\n", (void*) self->huffman_tree->root);

    self->last_node = 0;

    do {
        need_to_read = self->bufio_read_writer->size;

        if (left_to_read - need_to_read < 0) {
            need_to_read = left_to_read;
        }

        err = huf_read(reader, buf, &need_to_read);
        __assert__(err);

        __debug__("HERE\n");

        err = __huf_decode_partial(self, buf, need_to_read);
        __assert__(err);

        left_to_read -= need_to_read;
    } while (left_to_read);

    /*reader_length -= self->bufio_read_writer->have_been_written;*/
    /*err = huf_bufio_read_writer_flush(self->bufio_read_writer, reader_length);*/
    err = huf_bufio_read_writer_flush(self->bufio_read_writer);
    __assert__(err);

    __finally__;

    huf_decoder_free(&self);
    free(tree_head);

    __end__;
}
