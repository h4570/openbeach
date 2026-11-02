
#include "unit_test.h"

#include "core/hash_prime.h"

using obl::hash_prime::NextPrime;

OBL_TEST_CASE("hash_prime.picks_first_entry_not_below")
{
    OBL_CHECK_EQ(NextPrime(0), 53u);
    OBL_CHECK_EQ(NextPrime(53), 53u);
    OBL_CHECK_EQ(NextPrime(54), 97u);
    OBL_CHECK_EQ(NextPrime(4500), 6151u);
    OBL_CHECK_EQ(NextPrime(1610612741u), 1610612741u);
    OBL_CHECK_EQ(NextPrime(1610612742u), 3221225473u);
    OBL_CHECK_EQ(NextPrime(4294967291u), 4294967291u);
}

OBL_TEST_CASE("hash_prime.above_the_table_returns_the_argument")
{
    OBL_CHECK_EQ(NextPrime(4294967292u), 4294967292u);
    OBL_CHECK_EQ(NextPrime(0xffffffffu), 0xffffffffu);
}
