#include "test_array.h"
#include "../include/datoviz/array.h"



/*************************************************************************************************/
/*  Array tests                                                                                  */
/*************************************************************************************************/

int test_array_1(TestContext* context)
{
    uint8_t values[] = {1, 2, 3, 4, 5, 6};

    DvzArray arr = dvz_array(6, DVZ_DTYPE_CHAR);

    // 1:6
    dvz_array_data(&arr, 0, 6, 6, values);
    AT(memcmp(arr.data, values, sizeof(values)) == 0);

    // Partial copy of data.
    dvz_array_data(&arr, 2, 3, 3, values); // put [1, 2, 3] in arr[2:5]
    AT(memcmp(arr.data, (uint8_t[]){1, 2, 1, 2, 3, 6}, sizeof(values)) == 0);

    dvz_array_destroy(&arr);
    return 0;
}



int test_array_2(TestContext* context)
{
    float values[] = {1, 2, 3, 4, 5, 6};

    DvzArray arr = dvz_array(6, DVZ_DTYPE_FLOAT);

    // 1:6
    dvz_array_data(&arr, 0, 6, 6, values);
    AT(memcmp(arr.data, values, sizeof(values)) == 0);

    // Partial copy of data.
    dvz_array_data(&arr, 2, 3, 3, values); // put [1, 2, 3] in arr[2:5]
    AT(memcmp(arr.data, (float[]){1, 2, 1, 2, 3, 6}, sizeof(values)) == 0);

    // Resize with less elements. Should not lose the original data.
    dvz_array_resize(&arr, 3);
    AT(memcmp(arr.data, (float[]){1, 2, 1}, 3 * sizeof(float)) == 0);

    // Resize with more elements.
    dvz_array_resize(&arr, 9);
    // Resizing should repeat the last element.
    AT(memcmp(arr.data, (float[]){1, 2, 1, 2, 3, 6, 6, 6, 6}, 9 * sizeof(float)) == 0);

    // Fill the array with a constant value.
    float value = 12.0f;
    ASSERT(arr.item_count == 9);
    dvz_array_data(&arr, 0, 9, 1, &value);
    for (uint32_t i = 0; i < 9; i++)
    {
        AT(((float*)arr.data)[i] == value);
    }

    dvz_array_destroy(&arr);
    return 0;
}



typedef struct TestDtype TestDtype;
struct TestDtype
{
    uint8_t a;
    float b;
};



int test_array_3(TestContext* context)
{
    // uint8, float32
    DvzArray arr = dvz_array_struct(3, sizeof(TestDtype));

    TestDtype value = {1, 2};
    dvz_array_data(&arr, 0, 3, 1, &value);
    for (uint32_t i = 0; i < 3; i++)
    {
        AT(((TestDtype*)(dvz_array_item(&arr, i)))->a == 1);
        AT(((TestDtype*)(dvz_array_item(&arr, i)))->b == 2);
    }

    // Copy data to the second column.
    float b = 20.0f;
    dvz_array_column(
        &arr, offsetof(TestDtype, b), sizeof(float), 1, 2, 1, &b, 0, 0, DVZ_ARRAY_COPY_SINGLE, 1);

    // Row #0.
    AT(((TestDtype*)(dvz_array_item(&arr, 0)))->a == 1);
    AT(((TestDtype*)(dvz_array_item(&arr, 0)))->b == 2);

    // Row #1.
    AT(((TestDtype*)(dvz_array_item(&arr, 1)))->a == 1);
    AT(((TestDtype*)(dvz_array_item(&arr, 1)))->b == 20);

    // Row #2.
    AT(((TestDtype*)(dvz_array_item(&arr, 2)))->a == 1);
    AT(((TestDtype*)(dvz_array_item(&arr, 2)))->b == 20);

    // Resize.
    dvz_array_resize(&arr, 4);
    // Row #3
    AT(((TestDtype*)(dvz_array_item(&arr, 3)))->a == 1);
    AT(((TestDtype*)(dvz_array_item(&arr, 3)))->b == 20);

    dvz_array_destroy(&arr);
    return 0;
}



int test_array_4(TestContext* context)
{
    // uint8, float32
    DvzArray arr = dvz_array_struct(4, sizeof(TestDtype));
    TestDtype* item = NULL;
    float b[] = {0.5f, 2.5f};

    // Test single copy
    {
        dvz_array_column(
            &arr, offsetof(TestDtype, b), sizeof(float), 0, 4, 2, &b, 0, 0, DVZ_ARRAY_COPY_SINGLE,
            2);

        for (uint32_t i = 0; i < 4; i++)
        {
            item = dvz_array_item(&arr, i);
            AT(item->b == (i % 2 == 0 ? i + .5f : 0));
        }
    }

    dvz_array_clear(&arr);

    // Test repeat copy
    {
        dvz_array_column(
            &arr, offsetof(TestDtype, b), sizeof(float), 0, 4, 2, &b, 0, 0, DVZ_ARRAY_COPY_REPEAT,
            2);

        for (uint32_t i = 0; i < 4; i++)
        {
            item = dvz_array_item(&arr, i);
            AT(item->b == (i % 2 == 0 ? (i + .5f) : (i - .5f)));
        }
    }

    dvz_array_destroy(&arr);
    return 0;
}



int test_array_5(TestContext* context)
{
    uint8_t values[] = {1, 2, 3, 4, 5, 6};

    DvzArray arr = dvz_array(3, DVZ_DTYPE_CHAR);

    // Check automatic resize when setting larger data array.
    AT(arr.item_count == 3);
    dvz_array_data(&arr, 0, 6, 6, values);
    AT(arr.item_count == 6);
    AT(memcmp(arr.data, values, sizeof(values)) == 0);

    dvz_array_destroy(&arr);
    return 0;
}



int test_array_cast(TestContext* context)
{
    // uint8, float32
    DvzArray arr = dvz_array_struct(4, sizeof(TestDtype));
    TestDtype* item = NULL;
    double b[] = {0.5, 2.5};

    dvz_array_column(
        &arr, offsetof(TestDtype, b), sizeof(double), 0, 4, 2, &b, DVZ_DTYPE_DOUBLE,
        DVZ_DTYPE_FLOAT, DVZ_ARRAY_COPY_SINGLE, 2);

    for (uint32_t i = 0; i < 4; i++)
    {
        item = dvz_array_item(&arr, i);
        AT(item->b == (i % 2 == 0 ? i + .5 : 0));
    }

    dvz_array_destroy(&arr);
    return 0;
}



typedef struct _mvp _mvp;
struct _mvp
{
    mat4 model;
    mat4 view;
    mat4 proj;
};

int test_array_mvp(TestContext* context)
{
    DvzArray arr = dvz_array_struct(1, sizeof(_mvp));

    _mvp id = {0};
    glm_mat4_identity(id.model);
    glm_mat4_identity(id.view);
    glm_mat4_identity(id.proj);

    dvz_array_column(
        &arr, offsetof(_mvp, model), sizeof(mat4), 0, 1, 1, id.model, 0, 0, DVZ_ARRAY_COPY_SINGLE,
        1);

    dvz_array_column(
        &arr, offsetof(_mvp, view), sizeof(mat4), 0, 1, 1, id.view, 0, 0, DVZ_ARRAY_COPY_SINGLE,
        1);

    dvz_array_column(
        &arr, offsetof(_mvp, proj), sizeof(mat4), 0, 1, 1, id.proj, 0, 0, DVZ_ARRAY_COPY_SINGLE,
        1);

    _mvp* mvp = dvz_array_item(&arr, 0);
    for (uint32_t i = 0; i < 4; i++)
    {
        for (uint32_t j = 0; j < 4; j++)
        {
            AT(mvp->model[i][j] == (i == j ? 1 : 0));
            AT(mvp->view[i][j] == (i == j ? 1 : 0));
            AT(mvp->proj[i][j] == (i == j ? 1 : 0));
        }
    }

    dvz_array_destroy(&arr);
    return 0;
}



int test_array_3D(TestContext* context)
{
    DvzArray arr = dvz_array_3D(2, 2, 3, 1, DVZ_DTYPE_CHAR);

    uint8_t value = 12;
    dvz_array_data(&arr, 0, 6, 1, &value);

    void* data = arr.data;
    VkDeviceSize size = arr.buffer_size;
    uint8_t* item = dvz_array_item(&arr, 5);
    AT(*item == value);

    // Reshaping deletes the data.
    dvz_array_reshape(&arr, 3, 2, 1);
    item = dvz_array_item(&arr, 5);
    AT(*item == 0);
    // If the total size is the same, the data pointer doesn't change.
    AT(arr.data == data);
    AT(arr.buffer_size == size);

    // Reshaping deletes the data.
    dvz_array_reshape(&arr, 4, 3, 1);
    item = dvz_array_item(&arr, 5);
    AT(*item == 0);
    // If the total size is different, the data buffer is reallocated.
    AT(arr.buffer_size != size);

    dvz_array_destroy(&arr);
    return 0;
}
