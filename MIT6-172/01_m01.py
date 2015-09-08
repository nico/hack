import timeit
print '%f ms' % (1000 * timeit.timeit(
    'np.dot(np.random.rand(1024, 1024), np.random.rand(1024, 1024))',
    setup='import numpy as np', number=1))  # 67 ms.
