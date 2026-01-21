# Testing help

**Minimal test example (test_example.cpp):**

```cpp
#include <gtest/gtest.h>

TEST(MathTest, Addition) {
  EXPECT_EQ(2 + 2, 4);
}
```

**Build and run:**

```sh
g++ test_example.cpp -lgtest -lgtest_main -pthread -o test_example
./test_example
```

**List all tests (including specific ones):**

```sh
./test_example --gtest_list_tests
```

**Run only a specific test:**

```sh
./test_example --gtest_filter=MathTest.Addition
```

This will only run the MathTest.Addition test.
