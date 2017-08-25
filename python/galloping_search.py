# return -1 if not found, else return idx
def binary_search(array, beg, end, val):
    if beg == end:
        return -1
    mid = (beg + end) / 2
    if array[mid] == val:
        return mid
    return binary_search(array, beg, mid, val) if val < array[mid] else binary_search(array, mid + 1, end, val)


# return the last one <= val
def binary_search2(array, beg, end, val):
    assert 0 <= beg < end <= len(array)

    # beg <= mid < end
    mid = (beg + end) / 2
    if array[mid] == val:
        return True, mid

    if array[mid] < val:
        return (False, mid) if mid + 1 == end else binary_search2(array, mid + 1, end, val)

    if mid == beg:
        return False, beg
    return binary_search2(array, beg, mid, val)


def galloping_search_one_comp(array, beg, end, val):
    front_val = array[beg]
    if front_val > val:
        return 0, beg
    elif front_val == val:
        return 1, beg + 1

    prev_offset = 0
    jump_step = 1


if __name__ == '__main__':
    lst1 = [18, 20, 40]
    lst2 = range(30)

    test_val_lst = [20, 18, 40, 19, 41, -1,7, 70]

    print map(lambda val: binary_search(lst1, 0, len(lst1), val), test_val_lst)
    print map(lambda val: binary_search2(lst1, 0, len(lst1), val), test_val_lst)
