/*
This file is part of GtkRadiant.

GtkRadiant is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GtkRadiant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GtkRadiant; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "ufoai_filters.h"

#include "ibrush.h"
#include "ientity.h"

// believe me, i'm sorry
#include "../../radiant/brush.h"

#include "generic/callback.h"

#include <list>

int actorclip_active = 0;
int stepon_active = 0;
int level_active = 0;

// TODO: This should be added to ibrush.h
// like already done for Node_getEntity in ientity.h
// FIXME: Doesn't belong here
inline Brush* Node_getBrush(scene::Node& node)
{
	return NodeTypeCast<Brush>::cast(node);
}

void hide_node(scene::Node& node, bool hide)
{
	hide
	? node.enable(scene::Node::eHidden)
	: node.disable(scene::Node::eHidden);
}

typedef std::list<Entity*> entitylist_t;

class EntityFindByClassname : public scene::Graph::Walker
{
	const char* m_name;
	entitylist_t& m_entitylist;
	int m_flag;

public:
	EntityFindByClassname(const char* name, entitylist_t& entitylist, int flag)
		: m_name(name), m_entitylist(entitylist), m_flag(flag)
	{
	}
	bool pre(const scene::Path& path, scene::Instance& instance) const
	{
		int spawnflagsInt;
		Entity* entity = Node_getEntity(path.top());
		if (entity != 0)
		{
			if (string_equal(m_name, entity->getKeyValue("classname")))
			{
				const char *spawnflags = entity->getKeyValue("spawnflags");
				if (!string_empty(spawnflags)) {
					spawnflagsInt = atoi(spawnflags);
					if (!(spawnflagsInt & m_flag))
					{
						hide_node(path.top(), true); // hide
						m_entitylist.push_back(entity);
					}
					else
					{
						// maybe it was already hidden?
						hide_node(path.top(), false); // unhide
					}
				}
				else
				{
					globalOutputStream() << "no spawnflags for "<< m_name << ".\n";
				}
				globalOutputStream() << "entity match entity name we are searching for: "<< m_name << ".\n";
			}
			else
			{
				globalOutputStream() << "entity does not match entity name we are searching for: "<< m_name << ".\n";
			}
		}
		else
		{
			globalOutputStream() << "no entity: "<< m_name << ".\n";
		}
		return true;
	}
};

class ForEachFace : public BrushVisitor
{
	Brush &m_brush;
public:
	mutable int m_contentFlagsVis;
	mutable int m_surfaceFlagsVis;

	ForEachFace(Brush& brush)
		: m_brush(brush)
	{
		m_contentFlagsVis = -1;
		m_surfaceFlagsVis = -1;
	}

	void visit(Face& face) const
	{
#if 0
		if (m_surfaceFlagsVis < 0)
			m_surfaceFlagsVis = face.getShader().m_flags.m_surfaceFlags;
		else if (m_surfaceFlagsVis != face.getShader().m_flags.m_surfaceFlags)
			globalOutputStream() << "Faces with different surfaceflags at brush\n";
		if (m_contentFlagsVis < 0)
			m_contentFlagsVis = face.getShader().m_flags.m_contentFlags;
		else if (m_contentFlagsVis != face.getShader().m_flags.m_contentFlags)
			globalOutputStream() << "Faces with different contentflags at brush\n";
#endif
		m_surfaceFlagsVis = face.getShader().m_flags.m_surfaceFlags;
		m_contentFlagsVis = face.getShader().m_flags.m_contentFlags;
	}
};

typedef std::list<Brush*> brushlist_t;

class BrushGetLevel : public scene::Graph::Walker
{
	brushlist_t& m_brushlist;
	int m_flag;
	bool m_content; // if true - use m_contentFlags - otherwise m_surfaceFlags
	mutable bool m_notset;
	mutable bool hide;
public:
	BrushGetLevel(brushlist_t& brushlist, int flag, bool content, bool notset)
		: m_brushlist(brushlist), m_flag(flag), m_content(content), m_notset(notset)
	{
		hide = false;
	}
	bool pre(const scene::Path& path, scene::Instance& instance) const
	{
		Brush* brush = Node_getBrush(path.top());
		if (brush != 0)
		{
			if (!m_flag)
			{
				hide_node(path.top(), false);
			}
			else
			{
				ForEachFace faces(*brush);
				brush->forEachFace(faces);
				if (m_content)
				{
//					if (faces.m_contentFlagsVis > 0)
//						globalOutputStream() << "contentflags: " << faces.m_contentFlagsVis << " flag: " << m_flag << "\n";
					if (faces.m_contentFlagsVis > 0)
					{
						if (m_notset && (!(faces.m_contentFlagsVis & m_flag)))
						{
							hide_node(path.top(), true);
							hide = true;
						}
						else if (!m_notset && ((faces.m_contentFlagsVis & m_flag)))
						{
							hide_node(path.top(), true);
							hide = true;
						}
					}
				}
				else
				{
//					if (faces.m_surfaceFlagsVis > 0)
//						globalOutputStream() << "surfaceflags: " << faces.m_surfaceFlagsVis << " flag: " << m_flag << "\n";
					if (faces.m_surfaceFlagsVis > 0)
					{
						if (m_notset && (!(faces.m_surfaceFlagsVis & m_flag)))
						{
							hide_node(path.top(), true);
							hide = true;
						}
						else if (!m_notset && ((faces.m_surfaceFlagsVis & m_flag)))
						{
							hide_node(path.top(), true);
							hide = true;
						}
					}
				}

				/* make it visible again */
				if (!hide)
					hide_node(path.top(), false);
				else
					m_brushlist.push_back(brush);
			}
		}
		return true;
	}
};

/**
 * @brief Activates the level filter for the given level
 * @param[in] level Which level to show?
 * @todo Entities
 */
void filter_level(int flag)
{
	int level;

	level = (flag >> 8);

	if (level_active)
	{
		flag = 0;
		level_active = 0;
	}
	else
	{
		level_active = level;
	}

	brushlist_t brushes;
	entitylist_t entities;

	// first all brushes
	GlobalSceneGraph().traverse(BrushGetLevel(brushes, flag, true, true));

	// now all entities
	// TODO/FIXME: This currently doesn't result in any entities - i don't know why
	GlobalSceneGraph().traverse(EntityFindByClassname("func_breakable", entities, flag));
	GlobalSceneGraph().traverse(EntityFindByClassname("misc_model", entities, flag));

	if (brushes.empty())
	{
		globalOutputStream() << "UFO:AI: No brushes.\n";
	}
	else
	{
		globalOutputStream() << "UFO:AI: Found " << Unsigned(brushes.size()) << " brushes.\n";
	}

	// now let's filter all entities like misc_model and func_breakable that have the spawnflags set
	if (entities.empty())
	{
		globalOutputStream() << "UFO:AI: No entities.\n";
	}
	else
	{
		globalOutputStream() << "UFO:AI: Found " << Unsigned(entities.size()) << " entities.\n";
	}

	SceneChangeNotify();

	globalOutputStream() << "filter_level: " << level << " flag: " << flag << "\n";
}

void filter_stepon (void)
{
	int flag = CONTENTS_STEPON;
	if (stepon_active) {
		flag = 0;
		stepon_active = 0;
	} else {
		stepon_active = 1;
	}
	brushlist_t brushes;
	GlobalSceneGraph().traverse(BrushGetLevel(brushes, flag, true, false));

	if (brushes.empty())
	{
		globalOutputStream() << "UFO:AI: No brushes.\n";
	}
	else
	{
		globalOutputStream() << "UFO:AI: Hiding " << Unsigned(brushes.size()) << " stepon brushes.\n";
	}

	SceneChangeNotify();

	globalOutputStream() << "filter_stepon: flag: " << flag << "\n";
}

void filter_actorclip (void)
{
	int flag = CONTENTS_ACTORCLIP;
	if (actorclip_active) {
		flag = 0;
		actorclip_active = 0;
	} else {
		actorclip_active = 1;
	}
	actorclip_active = 1;
	brushlist_t brushes;
	GlobalSceneGraph().traverse(BrushGetLevel(brushes, flag, true, false));

	if (brushes.empty())
	{
		globalOutputStream() << "UFO:AI: No brushes.\n";
	}
	else
	{
		globalOutputStream() << "UFO:AI: Hiding " << Unsigned(brushes.size()) << " actorclip brushes.\n";
	}

	SceneChangeNotify();

	globalOutputStream() << "filter_actorclip: flag: " << flag << "\n";
}
