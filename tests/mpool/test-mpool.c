#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "mpool.h"

int main(void)
{
    assert(!mpool_alloc(NULL));
    assert(!mpool_calloc(NULL));
    assert(!mpool_create(SIZE_MAX, 1));
    assert(!mpool_create(1, SIZE_MAX));

    /* Small arenas force repeated extension while earlier chunks stay live. */
    struct mpool *pool = mpool_create(1, 1001);
    assert(pool);
    unsigned char *chunks[128];
    for (unsigned i = 0; i < 128; i++) {
        chunks[i] = mpool_calloc(pool);
        assert(chunks[i]);
        for (unsigned j = 0; j < 1001; j++)
            assert(chunks[i][j] == 0);
        memset(chunks[i], i + 1, 1001);
    }
    for (unsigned i = 0; i < 128; i++) {
        for (unsigned j = 0; j < 1001; j++)
            assert(chunks[i][j] == (unsigned char) (i + 1));
        mpool_free(pool, chunks[i]);
    }
    /* Reused storage must be cleared by calloc too. */
    unsigned char *reused = mpool_calloc(pool);
    assert(reused == chunks[127]);
    for (unsigned j = 0; j < 1001; j++)
        assert(reused[j] == 0);
    mpool_destroy(pool);
    mpool_destroy(NULL);
    return 0;
}
