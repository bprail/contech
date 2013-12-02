#include "ct_file.h"
void singleItemTest(bool compressed);
void multipleItemTest(bool compressed);

int main(int argc, char* argv[])
{
    printf("==Testing single (uncompressed)==\n\n");
    singleItemTest(false);
    printf("==Testing multiple (uncompressed)==\n\n");
    multipleItemTest(false);
    
    printf("==Testing single (compressed)==\n\n");
    singleItemTest(true);
    printf("==Testing multiple (compressed)==\n\n");
    multipleItemTest(true);
    return 0;
}


void singleItemTest(bool compressed){
    printf("Creating a ct_file handle...\n");
    ct_file* out = create_ct_file_w("testFile",compressed);
    unsigned int testInt = 481; 
    unsigned int testIntIn;
    
    
    printf("Writing String...\n");
    ct_write ( &testInt, sizeof(unsigned int), out );
    close_ct_file(out);

    printf("Calling ct_fread...\n");
    ct_file* in = create_ct_file_r("testFile");
    ct_read ( &testIntIn, sizeof(unsigned int), in );

    printf("Checking that task data was recovered correctly...\n");
    assert(testIntIn == testInt);

    printf("Test PASSED\n");
}

void multipleItemTest(bool compressed){
    printf("Creating a ct_file handle...\n");
    ct_file* out = create_ct_file_w("testFile",compressed);
    unsigned int testIntA = 485; 
    unsigned int testIntB = 924;
    unsigned int testIntC = 395;
    unsigned int testIntInA;
    unsigned int testIntInB;
    unsigned int testIntInC;
    
    printf("Writing String...\n");
    ct_write ( &testIntA, sizeof(unsigned int), out );
    ct_write ( &testIntB, sizeof(unsigned int), out );
    ct_write ( &testIntC, sizeof(unsigned int), out );
    close_ct_file(out);

    printf("Calling ct_fread...\n");
    ct_file* in = create_ct_file_r("testFile");
    ct_read ( &testIntInA, sizeof(unsigned int), in );
    ct_read ( &testIntInB, sizeof(unsigned int), in );
    ct_read ( &testIntInC, sizeof(unsigned int), in );

    printf("Checking that task data was recovered correctly...\n");
    assert(testIntInA == testIntA);
    assert(testIntInB == testIntB);
    assert(testIntInC == testIntC);
    printf("Test PASSED\n");
}

