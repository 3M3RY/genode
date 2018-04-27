/*
 * \brief  GUI element that has a hovered and selected state
 * \author Norman Feske
 * \date   2018-05-03
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SELECTABLE_ITEM_H_
#define _SELECTABLE_ITEM_H_

#include "hoverable_item.h"

namespace Sculpt_manager { struct Selectable_item; }


struct Sculpt_manager::Selectable_item : Hoverable_item
{
	typedef Hoverable_item::Id Id;

	Id _selected { };

	/**
	 * Apply click - if item is hovered, the click toggles the selection
	 */
	void toggle_selection_on_click()
	{
		if (_hovered.valid())
			_selected = (_hovered == _selected) ? Id() : _hovered;
	}

	void reset() { _selected = Id{}; }

	/**
	 * Return true if item is currently selected
	 */
	bool selected(Id const &id) const { return id == _selected; }

	/**
	 * Generate button attributes depending on the item state
	 */
	void gen_button_attr(Xml_generator &xml, Id const &id) const
	{
		Hoverable_item::gen_button_attr(xml, id);

		if (selected(id)) xml.attribute("selected", "yes");
	}
};

#endif /* _SELECTABLE_ITEM_H_ */
