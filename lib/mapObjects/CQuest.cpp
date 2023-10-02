/*
 * CQuest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CQuest.h"

#include <vcmi/spells/Spell.h>

#include "../ArtifactUtils.h"
#include "../NetPacks.h"
#include "../CSoundBase.h"
#include "../CGeneralTextHandler.h"
#include "../CHeroHandler.h"
#include "CGCreature.h"
#include "../IGameCallback.h"
#include "../mapObjectConstructors/CObjectClassesHandler.h"
#include "../serializer/JsonSerializeFormat.h"
#include "../GameConstants.h"
#include "../constants/StringConstants.h"
#include "../CSkillHandler.h"
#include "../mapping/CMap.h"
#include "../modding/ModScope.h"
#include "../modding/ModUtility.h"

VCMI_LIB_NAMESPACE_BEGIN


std::map <PlayerColor, std::set <ui8> > CGKeys::playerKeyMap;

//TODO: Remove constructor
CQuest::CQuest():
	qid(-1),
	missionType(MISSION_NONE),
	progress(NOT_ACTIVE),
	lastDay(-1),
	m13489val(0),
	textOption(0),
	completedOption(0),
	stackDirection(0),
	heroPortrait(-1),
	isCustomFirst(false),
	isCustomNext(false),
	isCustomComplete(false)
{
}

static std::string visitedTxt(const bool visited)
{
	int id = visited ? 352 : 353;
	return VLC->generaltexth->allTexts[id];
}

const std::string & CQuest::missionName(CQuest::Emission mission)
{
	static const std::array<std::string, 11> names = {
		"empty",
		"heroLevel",
		"primarySkill",
		"killHero",
		"killCreature",
		"bringArt",
		"bringCreature",
		"bringResources",
		"bringHero",
		"bringPlayer",
		"keymaster"
	};

	if(static_cast<size_t>(mission) < names.size())
		return names[static_cast<size_t>(mission)];
	return names[0];
}

const std::string & CQuest::missionState(int state)
{
	static const std::array<std::string, 5> states = {
		"receive",
		"visit",
		"complete",
		"hover",
		"description",
	};

	if(state < states.size())
		return states[state];
	return states[0];
}

bool CQuest::checkMissionArmy(const CQuest * q, const CCreatureSet * army)
{
	std::vector<CStackBasicDescriptor>::const_iterator cre;
	TSlots::const_iterator it;
	ui32 count = 0;
	ui32 slotsCount = 0;
	bool hasExtraCreatures = false;
	for(cre = q->m6creatures.begin(); cre != q->m6creatures.end(); ++cre)
	{
		for(count = 0, it = army->Slots().begin(); it != army->Slots().end(); ++it)
		{
			if(it->second->type == cre->type)
			{
				count += it->second->count;
				slotsCount++;
			}
		}

		if(static_cast<TQuantity>(count) < cre->count) //not enough creatures of this kind
			return false;

		hasExtraCreatures |= static_cast<TQuantity>(count) > cre->count;
	}

	return hasExtraCreatures || slotsCount < army->Slots().size();
}

bool CQuest::checkQuest(const CGHeroInstance * h) const
{
	switch (missionType)
	{
		case MISSION_NONE:
			return true;
		case MISSION_LEVEL:
			return m13489val <= h->level;
		case MISSION_PRIMARY_STAT:
			for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
			{
				if(h->getPrimSkillLevel(static_cast<PrimarySkill>(i)) < static_cast<int>(m2stats[i]))
					return false;
			}
			return true;
		case MISSION_KILL_HERO:
		case MISSION_KILL_CREATURE:
			if(!CGHeroInstance::cb->getObjByQuestIdentifier(m13489val))
				return true;
			return false;
		case MISSION_ART:
		{
			// if the object was deserialized
			if(artifactsRequirements.empty())
				for(const auto & id : m5arts)
					++artifactsRequirements[id];

			size_t reqSlots = 0;
			for(const auto & elem : artifactsRequirements)
			{
				// check required amount of artifacts
				if(h->getArtPosCount(elem.first, false, true, true) < elem.second)
					return false;
				if(!h->hasArt(elem.first))
					reqSlots += h->getAssemblyByConstituent(elem.first)->getPartsInfo().size() - 2;
			}
			if(ArtifactUtils::isBackpackFreeSlots(h, reqSlots))
				return true;
			else
				return false;
		}
		case MISSION_ARMY:
			return checkMissionArmy(this, h);
		case MISSION_RESOURCES:
			for(GameResID i = EGameResID::WOOD; i <= EGameResID::GOLD; ++i) //including Mithril ?
			{	//Quest has no direct access to callback
				if(CGHeroInstance::cb->getResource(h->tempOwner, i) < static_cast<int>(m7resources[i]))
					return false;
			}
			return true;
		case MISSION_HERO:
			return m13489val == h->type->getIndex();
		case MISSION_PLAYER:
			return m13489val == h->getOwner().getNum();
		default:
			return false;
	}
}

void CQuest::getVisitText(MetaString &iwText, std::vector<Component> &components, bool isCustom, bool firstVisit, const CGHeroInstance * h) const
{
	MetaString text;
	bool failRequirements = (h ? !checkQuest(h) : true);

	if(firstVisit)
	{
		isCustom = isCustomFirst;
		text = firstVisitText;
		iwText.appendRawString(text.toString());
	}
	else if(failRequirements)
	{
		isCustom = isCustomNext;
		text = nextVisitText;
		iwText.appendRawString(text.toString());
	}
	switch (missionType)
	{
		case MISSION_LEVEL:
			components.emplace_back(Component::EComponentType::EXPERIENCE, 0, m13489val, 0);
			if(!isCustom)
				iwText.replaceNumber(m13489val);
			break;
		case MISSION_PRIMARY_STAT:
		{
			MetaString loot;
			for(int i = 0; i < 4; ++i)
			{
				if(m2stats[i])
				{
					components.emplace_back(Component::EComponentType::PRIM_SKILL, i, m2stats[i], 0);
					loot.appendRawString("%d %s");
					loot.replaceNumber(m2stats[i]);
					loot.replaceRawString(VLC->generaltexth->primarySkillNames[i]);
				}
			}
			if (!isCustom)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case MISSION_KILL_HERO:
			components.emplace_back(Component::EComponentType::HERO_PORTRAIT, heroPortrait, 0, 0);
			if(!isCustom)
				addReplacements(iwText, text.toString());
			break;
		case MISSION_HERO:
			//FIXME: portrait may not match hero, if custom portrait was set in map editor
			components.emplace_back(Component::EComponentType::HERO_PORTRAIT, VLC->heroh->objects[m13489val]->imageIndex, 0, 0);
			if(!isCustom)
				iwText.replaceRawString(VLC->heroh->objects[m13489val]->getNameTranslated());
			break;
		case MISSION_KILL_CREATURE:
			{
				components.emplace_back(stackToKill);
				if(!isCustom)
				{
					addReplacements(iwText, text.toString());
				}
			}
			break;
		case MISSION_ART:
		{
			MetaString loot;
			for(const auto & elem : m5arts)
			{
				components.emplace_back(Component::EComponentType::ARTIFACT, elem, 0, 0);
				loot.appendRawString("%s");
				loot.replaceLocalString(EMetaText::ART_NAMES, elem);
			}
			if(!isCustom)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case MISSION_ARMY:
		{
			MetaString loot;
			for(const auto & elem : m6creatures)
			{
				components.emplace_back(elem);
				loot.appendRawString("%s");
				loot.replaceCreatureName(elem);
			}
			if(!isCustom)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case MISSION_RESOURCES:
		{
			MetaString loot;
			for(int i = 0; i < 7; ++i)
			{
				if(m7resources[i])
				{
					components.emplace_back(Component::EComponentType::RESOURCE, i, m7resources[i], 0);
					loot.appendRawString("%d %s");
					loot.replaceNumber(m7resources[i]);
					loot.replaceLocalString(EMetaText::RES_NAMES, i);
				}
			}
			if(!isCustom)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case MISSION_PLAYER:
			components.emplace_back(Component::EComponentType::FLAG, m13489val, 0, 0);
			if(!isCustom)
				iwText.replaceLocalString(EMetaText::COLOR, m13489val);
			break;
	}
}

void CQuest::getRolloverText(MetaString &ms, bool onHover) const
{
	// Quests with MISSION_NONE type don't have a text for them
	assert(missionType != MISSION_NONE);

	if(onHover)
		ms.appendRawString("\n\n");

	std::string questName = missionName(missionType);
	std::string questState = missionState(onHover ? 3 : 4);

	ms.appendRawString(VLC->generaltexth->translate("core.seerhut.quest", questName, questState,textOption));

	switch(missionType)
	{
		case MISSION_LEVEL:
			ms.replaceNumber(m13489val);
			break;
		case MISSION_PRIMARY_STAT:
			{
				MetaString loot;
				for (int i = 0; i < 4; ++i)
				{
					if (m2stats[i])
					{
						loot.appendRawString("%d %s");
						loot.replaceNumber(m2stats[i]);
						loot.replaceRawString(VLC->generaltexth->primarySkillNames[i]);
					}
				}
				ms.replaceRawString(loot.buildList());
			}
			break;
		case MISSION_KILL_HERO:
			ms.replaceRawString(heroName);
			break;
		case MISSION_KILL_CREATURE:
			ms.replaceCreatureName(stackToKill);
			break;
		case MISSION_ART:
			{
				MetaString loot;
				for(const auto & elem : m5arts)
				{
					loot.appendRawString("%s");
					loot.replaceLocalString(EMetaText::ART_NAMES, elem);
				}
				ms.replaceRawString(loot.buildList());
			}
			break;
		case MISSION_ARMY:
			{
				MetaString loot;
				for(const auto & elem : m6creatures)
				{
					loot.appendRawString("%s");
					loot.replaceCreatureName(elem);
				}
				ms.replaceRawString(loot.buildList());
			}
			break;
		case MISSION_RESOURCES:
			{
				MetaString loot;
				for (int i = 0; i < 7; ++i)
				{
					if (m7resources[i])
					{
						loot.appendRawString("%d %s");
						loot.replaceNumber(m7resources[i]);
						loot.replaceLocalString(EMetaText::RES_NAMES, i);
					}
				}
				ms.replaceRawString(loot.buildList());
			}
			break;
		case MISSION_HERO:
			ms.replaceRawString(VLC->heroh->objects[m13489val]->getNameTranslated());
			break;
		case MISSION_PLAYER:
			ms.replaceRawString(VLC->generaltexth->colors[m13489val]);
			break;
		default:
			break;
	}
}

void CQuest::getCompletionText(MetaString &iwText) const
{
	iwText.appendRawString(completedText.toString());
	switch(missionType)
	{
		case CQuest::MISSION_LEVEL:
			if (!isCustomComplete)
				iwText.replaceNumber(m13489val);
			break;
		case CQuest::MISSION_PRIMARY_STAT:
		{
			MetaString loot;
			assert(m2stats.size() <= 4);
			for (int i = 0; i < m2stats.size(); ++i)
			{
				if (m2stats[i])
				{
					loot.appendRawString("%d %s");
					loot.replaceNumber(m2stats[i]);
					loot.replaceRawString(VLC->generaltexth->primarySkillNames[i]);
				}
			}
			if (!isCustomComplete)
				iwText.replaceRawString(loot.buildList());
			break;
		}
		case CQuest::MISSION_ART:
		{
			MetaString loot;
			for(const auto & elem : m5arts)
			{
				loot.appendRawString("%s");
				loot.replaceLocalString(EMetaText::ART_NAMES, elem);
			}
			if (!isCustomComplete)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case CQuest::MISSION_ARMY:
		{
			MetaString loot;
			for(const auto & elem : m6creatures)
			{
				loot.appendRawString("%s");
				loot.replaceCreatureName(elem);
			}
			if (!isCustomComplete)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case CQuest::MISSION_RESOURCES:
		{
			MetaString loot;
			for (int i = 0; i < 7; ++i)
			{
				if (m7resources[i])
				{
					loot.appendRawString("%d %s");
					loot.replaceNumber(m7resources[i]);
					loot.replaceLocalString(EMetaText::RES_NAMES, i);
				}
			}
			if (!isCustomComplete)
				iwText.replaceRawString(loot.buildList());
		}
			break;
		case MISSION_KILL_HERO:
		case MISSION_KILL_CREATURE:
			if (!isCustomComplete)
				addReplacements(iwText, completedText.toString());
			break;
		case MISSION_HERO:
			if (!isCustomComplete)
				iwText.replaceRawString(VLC->heroh->objects[m13489val]->getNameTranslated());
			break;
		case MISSION_PLAYER:
			if (!isCustomComplete)
				iwText.replaceRawString(VLC->generaltexth->colors[m13489val]);
			break;
	}
}

void CQuest::addArtifactID(const ArtifactID & id)
{
	m5arts.push_back(id);
	++artifactsRequirements[id];
}

void CQuest::serializeJson(JsonSerializeFormat & handler, const std::string & fieldName)
{
	auto q = handler.enterStruct(fieldName);

	handler.serializeStruct("firstVisitText", firstVisitText);
	handler.serializeStruct("nextVisitText", nextVisitText);
	handler.serializeStruct("completedText", completedText);

	if(!handler.saving)
	{
		isCustomFirst = !firstVisitText.empty();
		isCustomNext = !nextVisitText.empty();
		isCustomComplete = !completedText.empty();
	}

	static const std::vector<std::string> MISSION_TYPE_JSON =
	{
		"None", "Level", "PrimaryStat", "KillHero", "KillCreature", "Artifact", "Army", "Resources", "Hero", "Player"
	};

	handler.serializeEnum("missionType", missionType, Emission::MISSION_NONE, MISSION_TYPE_JSON);
	handler.serializeInt("timeLimit", lastDay, -1);

	switch (missionType)
	{
	case MISSION_NONE:
		break;
	case MISSION_LEVEL:
		handler.serializeInt("heroLevel", m13489val, -1);
		break;
	case MISSION_PRIMARY_STAT:
		{
			auto primarySkills = handler.enterStruct("primarySkills");
			if(!handler.saving)
				m2stats.resize(GameConstants::PRIMARY_SKILLS);

			for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
				handler.serializeInt(NPrimarySkill::names[i], m2stats[i], 0);
		}
		break;
	case MISSION_KILL_HERO:
	case MISSION_KILL_CREATURE:
		handler.serializeInstance<ui32>("killTarget", m13489val, static_cast<ui32>(-1));
		break;
	case MISSION_ART:
		//todo: ban artifacts
		handler.serializeIdArray<ArtifactID>("artifacts", m5arts);
		break;
	case MISSION_ARMY:
		{
			auto a = handler.enterArray("creatures");
			a.serializeStruct(m6creatures);
		}
		break;
	case MISSION_RESOURCES:
		{
			auto r = handler.enterStruct("resources");

			for(size_t idx = 0; idx < (GameConstants::RESOURCE_QUANTITY - 1); idx++)
			{
				handler.serializeInt(GameConstants::RESOURCE_NAMES[idx], m7resources[idx], 0);
			}
		}
		break;
	case MISSION_HERO:
		handler.serializeId<ui32, ui32, HeroTypeID>("hero", m13489val, 0);
		break;
	case MISSION_PLAYER:
		handler.serializeId<ui32, ui32, PlayerColor>("player", m13489val, PlayerColor::NEUTRAL);
		break;
	default:
		logGlobal->error("Invalid quest mission type");
		break;
	}

}

void CGSeerHut::setObjToKill()
{
	if(quest->missionType == CQuest::MISSION_KILL_CREATURE)
	{
		quest->stackToKill = getCreatureToKill(false)->getStack(SlotID(0)); //FIXME: stacks tend to disappear (desync?) on server :?
		assert(quest->stackToKill.type);
		quest->stackToKill.count = 0; //no count in info window
		quest->stackDirection = checkDirection();
	}
	else if(quest->missionType == CQuest::MISSION_KILL_HERO)
	{
		quest->heroName = getHeroToKill(false)->getNameTranslated();
		quest->heroPortrait = getHeroToKill(false)->portrait;
	}

	quest->getCompletionText(configuration.onSelect);
	for(auto & i : configuration.info)
		quest->getCompletionText(i.message);
}

void CGSeerHut::init(CRandomGenerator & rand)
{
	auto names = VLC->generaltexth->findStringsWithPrefix("core.seerhut.names");

	auto seerNameID = *RandomGeneratorUtil::nextItem(names, rand);
	seerName = VLC->generaltexth->translate(seerNameID);
	quest->textOption = rand.nextInt(2);
	quest->completedOption = rand.nextInt(1, 3);
	
	configuration.canRefuse = true;
	configuration.visitMode = Rewardable::EVisitMode::VISIT_ONCE;
	configuration.selectMode = Rewardable::ESelectMode::SELECT_PLAYER;
}

void CGSeerHut::initObj(CRandomGenerator & rand)
{
	init(rand);
	
	CRewardableObject::initObj(rand);

	quest->progress = CQuest::NOT_ACTIVE;
	if(quest->missionType)
	{
		std::string questName  = quest->missionName(quest->missionType);

		if(!quest->isCustomFirst)
			quest->firstVisitText.appendTextID(TextIdentifier("core", "seerhut", "quest", questName, quest->missionState(0), quest->textOption).get());
		if(!quest->isCustomNext)
			quest->firstVisitText.appendTextID(TextIdentifier("core", "seerhut", "quest", questName, quest->missionState(1), quest->textOption).get());
		if(!quest->isCustomComplete)
			quest->firstVisitText.appendTextID(TextIdentifier("core", "seerhut", "quest", questName, quest->missionState(2), quest->textOption).get());
	}
	else
	{
		quest->progress = CQuest::COMPLETE;
		quest->firstVisitText.appendTextID(TextIdentifier("core", "seehut", "empty", quest->completedOption).get());
	}
}

void CGSeerHut::getRolloverText(MetaString &text, bool onHover) const
{
	quest->getRolloverText (text, onHover);//TODO: simplify?
	if(!onHover)
		text.replaceRawString(seerName);
}

std::string CGSeerHut::getHoverText(PlayerColor player) const
{
	std::string hoverName = getObjectName();
	if(ID == Obj::SEER_HUT && quest->progress != CQuest::NOT_ACTIVE)
	{
		hoverName = VLC->generaltexth->allTexts[347];
		boost::algorithm::replace_first(hoverName, "%s", seerName);
	}

	if(quest->progress & quest->missionType) //rollover when the quest is active
	{
		MetaString ms;
		getRolloverText (ms, true);
		hoverName += ms.toString();
	}
	return hoverName;
}

void CQuest::addReplacements(MetaString &out, const std::string &base) const
{
	switch(missionType)
	{
	case MISSION_KILL_CREATURE:
		if(stackToKill.type)
		{
			out.replaceCreatureName(stackToKill);
			if (std::count(base.begin(), base.end(), '%') == 2) //say where is placed monster
			{
				out.replaceRawString(VLC->generaltexth->arraytxt[147+stackDirection]);
			}
		}
		break;
	case MISSION_KILL_HERO:
		out.replaceTextID(heroName);
		break;
	}
}

bool IQuestObject::checkQuest(const CGHeroInstance* h) const
{
	return quest->checkQuest(h);
}

void IQuestObject::getVisitText (MetaString &text, std::vector<Component> &components, bool isCustom, bool FirstVisit, const CGHeroInstance * h) const
{
	quest->getVisitText (text,components, isCustom, FirstVisit, h);
}

void IQuestObject::afterAddToMapCommon(CMap * map) const
{
	map->addNewQuestInstance(quest);
}

void CGSeerHut::setPropertyDer (ui8 what, ui32 val)
{
	switch(what)
	{
		case 10:
			quest->progress = static_cast<CQuest::Eprogress>(val);
			break;
	}
}

void CGSeerHut::newTurn(CRandomGenerator & rand) const
{
	CRewardableObject::newTurn(rand);
	if(quest->lastDay >= 0 && quest->lastDay <= cb->getDate() - 1) //time is up
	{
		cb->setObjProperty (id, CGSeerHut::OBJPROP_VISITED, CQuest::COMPLETE);
	}
}

void CGSeerHut::onHeroVisit(const CGHeroInstance * h) const
{
	InfoWindow iw;
	iw.player = h->getOwner();
	if(quest->progress < CQuest::COMPLETE)
	{
		bool firstVisit = !quest->progress;
		bool failRequirements = !checkQuest(h);
		bool isCustom = false;

		if(firstVisit)
		{
			isCustom = quest->isCustomFirst;
			cb->setObjProperty(id, CGSeerHut::OBJPROP_VISITED, CQuest::IN_PROGRESS);

			AddQuest aq;
			aq.quest = QuestInfo (quest, this, visitablePos());
			aq.player = h->tempOwner;
			cb->sendAndApply(&aq); //TODO: merge with setObjProperty?
		}
		else if(failRequirements)
		{
			isCustom = quest->isCustomNext;
		}

		if(firstVisit || failRequirements)
		{
			getVisitText (iw.text, iw.components, isCustom, firstVisit, h);

			cb->showInfoDialog(&iw);
		}
		if(!failRequirements) // propose completion, also on first visit
		{
			CRewardableObject::onHeroVisit(h);
			return;
		}
	}
	else
	{
		iw.text.appendRawString(VLC->generaltexth->seerEmpty[quest->completedOption]);
		if (ID == Obj::SEER_HUT)
			iw.text.replaceRawString(seerName);
		cb->showInfoDialog(&iw);
	}
}

int CGSeerHut::checkDirection() const
{
	int3 cord = getCreatureToKill()->pos;
	if(static_cast<double>(cord.x) / static_cast<double>(cb->getMapSize().x) < 0.34) //north
	{
		if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.34) //northwest
			return 8;
		else if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.67) //north
			return 1;
		else //northeast
			return 2;
	}
	else if(static_cast<double>(cord.x) / static_cast<double>(cb->getMapSize().x) < 0.67) //horizontal
	{
		if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.34) //west
			return 7;
		else if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.67) //central
			return 9;
		else //east
			return 3;
	}
	else //south
	{
		if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.34) //southwest
			return 6;
		else if(static_cast<double>(cord.y) / static_cast<double>(cb->getMapSize().y) < 0.67) //south
			return 5;
		else //southeast
			return 4;
	}
}

void CGSeerHut::completeQuest() const //reward
{
	cb->setObjProperty(id, CGSeerHut::OBJPROP_VISITED, CQuest::COMPLETE); //mission complete
}

const CGHeroInstance * CGSeerHut::getHeroToKill(bool allowNull) const
{
	const CGObjectInstance *o = cb->getObjByQuestIdentifier(quest->m13489val);
	if(allowNull && !o)
		return nullptr;
	assert(o && (o->ID == Obj::HERO  ||  o->ID == Obj::PRISON));
	return dynamic_cast<const CGHeroInstance *>(o);
}

const CGCreature * CGSeerHut::getCreatureToKill(bool allowNull) const
{
	const CGObjectInstance *o = cb->getObjByQuestIdentifier(quest->m13489val);
	if(allowNull && !o)
		return nullptr;
	assert(o && o->ID == Obj::MONSTER);
	return dynamic_cast<const CGCreature *>(o);
}

void CGSeerHut::blockingDialogAnswered(const CGHeroInstance *hero, ui32 answer) const
{
	CRewardableObject::blockingDialogAnswered(hero, answer);
	if(answer)
		completeQuest();
}

void CGSeerHut::afterAddToMap(CMap* map)
{
	IQuestObject::afterAddToMapCommon(map);
}

void CGSeerHut::serializeJsonOptions(JsonSerializeFormat & handler)
{
	//quest and reward
	CRewardableObject::serializeJsonOptions(handler);
	quest->serializeJson(handler, "quest");
	
	if(!handler.saving)
	{
		//backward compatibility for VCMI maps that use old SeerHut format
		auto s = handler.enterStruct("reward");
		const JsonNode & rewardsJson = handler.getCurrent();
		
		std::string fullIdentifier;
		std::string metaTypeName;
		std::string scope;
		std::string identifier;

		auto iter = rewardsJson.Struct().begin();
		fullIdentifier = iter->first;

		ModUtility::parseIdentifier(fullIdentifier, scope, metaTypeName, identifier);
		if(!std::set<std::string>{"resource", "primarySkill", "secondarySkill", "artifact", "spell", "creature", "experience", "mana", "morale", "luck"}.count(metaTypeName))
			return;

		int val = 0;
		handler.serializeInt(fullIdentifier, val);
		
		Rewardable::VisitInfo vinfo;
		auto & reward = vinfo.reward;
		if(metaTypeName == "experience")
		   reward.heroExperience = val;
		if(metaTypeName == "mana")
			reward.manaDiff = val;
		if(metaTypeName == "morale")
			reward.bonuses.emplace_back(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::OBJECT, val, id);
		if(metaTypeName == "luck")
			reward.bonuses.emplace_back(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OBJECT, val, id);
		if(metaTypeName == "resource")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.resources[rawId] = val;
		}
		if(metaTypeName == "primarySkill")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.primary.at(rawId) = val;
		}
		if(metaTypeName == "secondarySkill")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.secondary[rawId] = val;
		}
		if(metaTypeName == "artifact")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.artifacts.push_back(rawId);
		}
		if(metaTypeName == "spell")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.spells.push_back(rawId);
		}
		if(metaTypeName == "creature")
		{
			auto rawId = *VLC->identifiers()->getIdentifier(ModScope::scopeMap(), fullIdentifier, false);
			reward.creatures.emplace_back(rawId, val);
		}
		
		vinfo.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
		configuration.info.push_back(vinfo);
	}
}

void CGQuestGuard::init(CRandomGenerator & rand)
{
	blockVisit = true;
	quest->textOption = rand.nextInt(3, 5);
	quest->completedOption = rand.nextInt(4, 5);
	
	configuration.info.push_back({});
	configuration.info.back().visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	configuration.info.back().reward.removeObject = true;
	configuration.canRefuse = true;
}

void CGQuestGuard::serializeJsonOptions(JsonSerializeFormat & handler)
{
	//quest only, do not call base class
	quest->serializeJson(handler, "quest");
}

void CGKeys::reset()
{
	playerKeyMap.clear();
}

void CGKeys::setPropertyDer (ui8 what, ui32 val) //101-108 - enable key for player 1-8
{
	if (what >= 101 && what <= (100 + PlayerColor::PLAYER_LIMIT_I))
	{
		PlayerColor player(what-101);
		playerKeyMap[player].insert(static_cast<ui8>(val));
	}
	else
		logGlobal->error("Unexpected properties requested to set: what=%d, val=%d", static_cast<int>(what), val);
}

bool CGKeys::wasMyColorVisited(const PlayerColor & player) const
{
	return playerKeyMap.count(player) && vstd::contains(playerKeyMap[player], subID);
}

std::string CGKeys::getHoverText(PlayerColor player) const
{
	return getObjectName() + "\n" + visitedTxt(wasMyColorVisited(player));
}

std::string CGKeys::getObjectName() const
{
	return VLC->generaltexth->tentColors[subID] + " " + CGObjectInstance::getObjectName();
}

bool CGKeymasterTent::wasVisited (PlayerColor player) const
{
	return wasMyColorVisited (player);
}

void CGKeymasterTent::onHeroVisit( const CGHeroInstance * h ) const
{
	int txt_id;
	if (!wasMyColorVisited (h->getOwner()) )
	{
		cb->setObjProperty(id, h->tempOwner.getNum()+101, subID);
		txt_id=19;
	}
	else
		txt_id=20;
	h->showInfoDialog(txt_id);
}

void CGBorderGuard::initObj(CRandomGenerator & rand)
{
	blockVisit = true;
}

void CGBorderGuard::getVisitText (MetaString &text, std::vector<Component> &components, bool isCustom, bool FirstVisit, const CGHeroInstance * h) const
{
	text.appendLocalString(EMetaText::ADVOB_TXT,18);
}

void CGBorderGuard::getRolloverText (MetaString &text, bool onHover) const
{
	if (!onHover)
	{
		text.appendRawString(VLC->generaltexth->tentColors[subID]);
		text.appendRawString(" ");
		text.appendRawString(VLC->objtypeh->getObjectName(Obj::KEYMASTER, subID));
	}
}

bool CGBorderGuard::checkQuest(const CGHeroInstance * h) const
{
	return wasMyColorVisited (h->tempOwner);
}

void CGBorderGuard::onHeroVisit(const CGHeroInstance * h) const
{
	if (wasMyColorVisited (h->getOwner()) )
	{
		BlockingDialog bd (true, false);
		bd.player = h->getOwner();
		bd.text.appendLocalString (EMetaText::ADVOB_TXT, 17);
		cb->showBlockingDialog (&bd);
	}
	else
	{
		h->showInfoDialog(18);

		AddQuest aq;
		aq.quest = QuestInfo (quest, this, visitablePos());
		aq.player = h->tempOwner;
		cb->sendAndApply (&aq);
		//TODO: add this quest only once OR check for multiple instances later
	}
}

void CGBorderGuard::blockingDialogAnswered(const CGHeroInstance *hero, ui32 answer) const
{
	if (answer)
		cb->removeObject(this, hero->getOwner());
}

void CGBorderGuard::afterAddToMap(CMap * map)
{
	IQuestObject::afterAddToMapCommon(map);
}

void CGBorderGate::onHeroVisit(const CGHeroInstance * h) const //TODO: passability
{
	if (!wasMyColorVisited (h->getOwner()) )
	{
		h->showInfoDialog(18);

		AddQuest aq;
		aq.quest = QuestInfo (quest, this, visitablePos());
		aq.player = h->tempOwner;
		cb->sendAndApply (&aq);
	}
}

bool CGBorderGate::passableFor(PlayerColor color) const
{
	return wasMyColorVisited(color);
}

VCMI_LIB_NAMESPACE_END
