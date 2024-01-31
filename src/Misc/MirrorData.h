/*
    MirrorData.h - Component to store and provide data for the GUI mirrored from Core

    Copyright 2024,  Ichthyostega

    This file is part of yoshimi, which is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the License,
    or (at your option) any later version.

    yoshimi is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  See the GNU General Public License (version 2
    or later) for more details.

    You should have received a copy of the GNU General Public License
    along with yoshimi.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef MIRROR_DATA_H
#define MIRROR_DATA_H

#include "globals.h"
#include "Interface/GuiDataExchange.h"

#include <cassert>


/**
 * A »data mirror« component for the GUI.
 * As part of the GuiDataExchange protocol, this component is attached to some
 * GUI window or control and will then receive data updates pushed by the Core.
 */
template<class DAT>
class MirrorData
    : public GuiDataExchange::Subscription
{
    DAT data;

    void pushUpdate(GuiDataExchange::RoutingTag const& tag, void* buffer)  override
    {
        assert(tag.verifyType<DAT>());
        assert(buffer);
        data.~DAT(); // copy-construct into data storage
        new(&data) DAT{* reinterpret_cast<DAT*>(buffer)};
    }

public:
    MirrorData(GuiDataExchange::Connection<DAT> con)
        : Subscription{con}
        , data{}
        { }

    MirrorData(GuiDataExchange& hub, GuiDataExchange::RoutingTag tag)
        : MirrorData{GuiDataExchange::Connection<DAT>{hub,tag}}
        { }


    DAT& get()
    {
        return data;
    }
};

#endif /*MIRROR_DATA_H*/
