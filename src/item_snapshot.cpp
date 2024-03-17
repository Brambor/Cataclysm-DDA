#include "item_snapshot.h"


class item_snapshot_impl
{
};

// These need to be here so that pimpl works with unique ptr
item_snapshot::item_snapshot() = default;
item_snapshot::~item_snapshot() = default;

bool item_snapshot::store()
{
    return true;
}

void item_snapshot::load()
{
}

void item_snapshot::show()
{
}
