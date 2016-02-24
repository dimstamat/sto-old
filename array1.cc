#include <string>
#include <iostream>
#include <assert.h>
#include <vector>
#include "Transaction.hh"
#include "Array1.hh"

void testSimpleInt() {
	Array1<int, 100> f;

        {
            TransactionGuard t;
            f.transPut(1, 100);
        }

	{
            TransactionGuard t2;
            int f_read = f.transGet(1);
            assert(f_read == 100);
        }

	printf("PASS: testSimpleInt\n");
}

void testSimpleString() {
	Array1<std::string, 100> f;

	{
            TransactionGuard t;
            f.transPut(1, "100");
        }

	{
            TransactionGuard t2;
            std::string f_read = f.transGet(1);
            assert(f_read.compare("100") == 0);
        }

	printf("PASS: testSimpleString\n");
}

void testIter() {
    std::vector<int> arr;
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        int x = rand();
        arr.push_back(x);
        f.unsafe_put(i, x);
    }
    int max;
    TRANSACTION {
        max = *(std::max_element(f.begin(), f.end()));
    } RETRY(false);
    
    assert(max == *(std::max_element(arr.begin(), arr.end())));
    printf("Max is %i\n", max);
    printf("PASS: array max_element test\n");
    
}


void testConflictingIter() {
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        f.unsafe_put(i, i);
    }

    {
        TestTransaction t(1);
        std::max_element(f.begin(), f.end());

        TestTransaction t1(2);
        f.transPut(4, 10);
        assert(t1.try_commit());
        assert(!t.try_commit());
        printf("PASS: conflicting array max_element test\n");
    }
}

void testModifyingIter() {
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        f.unsafe_put(i, i);
    }

    {
        TransactionGuard t;
        std::replace(f.begin(), f.end(), 4, 6);
    }

    {
        TransactionGuard t1;
        int v = f.transGet(4);
        assert(v == 6);
    }

    printf("PASS: array replace test\n");
}

void testConflictingModifyIter1() {
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        f.unsafe_put(i, i);
    }
    
    {
        TestTransaction t(1);
        std::replace(f.begin(), f.end(), 4, 6);
    
        TestTransaction t1(2);
        f.transPut(4, 10);

        assert(t1.try_commit());
        assert(!t.try_commit());
    }

    {
        TransactionGuard t2;
        int v = f.transGet(4);
        assert(v == 10);
    }
    
    printf("PASS: array conflicting replace test1\n");
}

void testConflictingModifyIter2() {
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        f.unsafe_put(i, i);
    }

    {
        TransactionGuard t;
        std::replace(f.begin(), f.end(), 4, 6);
    }

    {
        TransactionGuard t1;
        f.transPut(4, 10);
    }

    {
        TransactionGuard t2;
        int v = f.transGet(4);
        assert(v == 10);
    }

    printf("PASS: array conflicting replace test2\n");
}

void testConflictingModifyIter3() {
    Array1<int, 10> f;
    for (int i = 0; i < 10; i++) {
        f.unsafe_put(i, i);
    }
    
    {
        TestTransaction t1(1);
        f.transGet(4);
    
        TestTransaction t(2);
        std::replace(f.begin(), f.end(), 4, 6);

        assert(t.try_commit());
        assert(!t1.try_commit());
    }

    {
        TransactionGuard t2;
        int v = f.transGet(4);
        assert(v == 6);
    }

    printf("PASS: array conflicting replace test3\n");
}


int main() {
    testSimpleInt();
    testSimpleString();
    testIter();
    testConflictingIter();
    testModifyingIter();
    testConflictingModifyIter1();
    testConflictingModifyIter2();
    testConflictingModifyIter3();
    return 0;
}
