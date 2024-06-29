#include <cstdint>
#include <functional>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/WorldContext.h>
#include <GWCA/GWCA.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Hero.h>
#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Utilities/Scanner.h>

#include "ActionsBase.h"
#include "DataHero.h"
#include "DataPlayer.h"
#include "HelperHero.h"
#include "HelperSkill.h"

bool HeroUseSkill(const uint32_t target_agent_id, const uint32_t skill_idx, const uint32_t hero_idx_zero_based)
{
    auto hero_action = GW::UI::ControlAction_Hero1Skill1;
    if (hero_idx_zero_based == 0)
        hero_action = GW::UI::ControlAction_Hero1Skill1;
    else if (hero_idx_zero_based == 1)
        hero_action = GW::UI::ControlAction_Hero2Skill1;
    else if (hero_idx_zero_based == 2)
        hero_action = GW::UI::ControlAction_Hero3Skill1;
    else if (hero_idx_zero_based == 3)
        hero_action = GW::UI::ControlAction_Hero4Skill1;
    else if (hero_idx_zero_based == 4)
        hero_action = GW::UI::ControlAction_Hero5Skill1;
    else if (hero_idx_zero_based == 5)
        hero_action = GW::UI::ControlAction_Hero6Skill1;
    else if (hero_idx_zero_based == 6)
        hero_action = GW::UI::ControlAction_Hero7Skill1;
    else
        return false;

    const auto curr_target_id = GW::Agents::GetTargetId();
    auto success = true;

    GW::GameThread::Enqueue([=, &success] {
        if (target_agent_id && target_agent_id != GW::Agents::GetTargetId())
            success &= GW::Agents::ChangeTarget(target_agent_id);
        const auto keypress_id = (GW::UI::ControlAction)(static_cast<uint32_t>(hero_action) + skill_idx);
        success &= GW::UI::Keypress(keypress_id);
        if (curr_target_id && target_agent_id != curr_target_id)
            success &= GW::Agents::ChangeTarget(curr_target_id);
    });

    return success;
}

bool HeroCastSkillIfAvailable(const Hero &hero,

                              const GW::Constants::SkillID skill_id,
                              std::function<bool(const Hero &)> cb_fn,
                              const TargetLogic target_logic,
                              const uint32_t target_id)
{
    if (!hero.hero_living || !hero.hero_living->agent_id)
        return false;

    if (!cb_fn(hero))
        return false;

    const auto [skill_idx, can_cast_skill] = SkillIdxOfHero(hero, skill_id);
    const auto has_skill_in_skillbar = skill_idx != static_cast<uint32_t>(-1);
    const auto player_id = GW::Agents::GetPlayerId();

    if (has_skill_in_skillbar && can_cast_skill)
    {
        switch (target_logic)
        {
        case TargetLogic::SEARCH_TARGET:
        case TargetLogic::PLAYER_TARGET:
        {
            const auto target = GW::Agents::GetTarget();
            if (target)
                return HeroUseSkill(target->agent_id, skill_idx, hero.hero_idx_zero_based);
            else
                return HeroUseSkill(player_id, skill_idx, hero.hero_idx_zero_based);
        }
        case TargetLogic::NO_TARGET:
        default:
        {
            return HeroUseSkill(player_id, skill_idx, hero.hero_idx_zero_based);
        }
        }
    }

    return false;
}


std::tuple<uint32_t, bool> SkillIdxOfHero(const Hero &hero, const GW::Constants::SkillID skill_id)
{
    constexpr static auto invalid_case = std::make_tuple(static_cast<uint32_t>(-1), false);

    if (!hero.hero_living)
        return invalid_case;

    const auto hero_energy =
        static_cast<uint32_t>(hero.hero_living->energy * static_cast<float>(hero.hero_living->max_energy));

    auto skill_idx = 0U;
    const auto hero_skills = GetAgentSkillbar(hero.hero_living->agent_id);
    if (!hero_skills)
        return invalid_case;

    for (const auto &skill : hero_skills->skills)
    {
        const auto has_skill_in_skillbar = skill.skill_id == skill_id;
        if (!has_skill_in_skillbar)
        {
            ++skill_idx;
            continue;
        }

        const auto *skill_data = GW::SkillbarMgr::GetSkillConstantData(skill_id);
        if (!skill_data)
        {
            ++skill_idx;
            continue;
        }

        const auto can_cast_skill = skill.GetRecharge() == 0 && hero_energy >= skill_data->GetEnergyCost();
        return std::make_tuple(skill_idx, can_cast_skill);
    }

    return invalid_case;
}

void SetHerosBehaviour(const uint32_t player_login_number, const GW::HeroBehavior hero_behaviour)
{
    const auto *const party_info = GW::PartyMgr::GetPartyInfo();
    if (!party_info || !party_info->heroes.valid())
        return;

    for (const auto &hero : party_info->heroes)
    {
        if (hero.owner_player_id == player_login_number)
            GW::PartyMgr::SetHeroBehavior(hero.agent_id, hero_behaviour);
    }
}

bool HeroSkill_StartConditions(const GW::Constants::SkillID skill_id,
                               const long wait_ms,
                               const bool ignore_effect_agent_id)
{
    if (!ActionABC::HasWaitedLongEnough(wait_ms))
        return false;

    if (skill_id != GW::Constants::SkillID::No_Skill)
        return true;

    if (PlayerHasEffect(skill_id, ignore_effect_agent_id))
        return false;

    return true;
}

bool SmartUseSkill(const GW::Constants::SkillID skill_id,
                   const GW::Constants::Profession skill_class,
                   const std::string_view skill_name,
                   const HeroData &hero_data,
                   std::function<bool()> player_conditions,
                   std::function<bool(const Hero &)> hero_conditions,
                   const long wait_ms,
                   const TargetLogic target_logic,
                   const uint32_t current_target_id,
                   const bool ignore_effect_agent_id)
{
    if (!HeroSkill_StartConditions(skill_id, wait_ms, ignore_effect_agent_id))
        return false;

    if (!player_conditions())
        return false;

    if (hero_data.hero_class_idx_map.find(skill_class) == hero_data.hero_class_idx_map.end())
        return false;

    auto hero_idxs_zero_based = hero_data.hero_class_idx_map.at(skill_class);
    if (hero_idxs_zero_based.size() == 0)
        return false;

    for (const auto hero_idx_zero_based : hero_idxs_zero_based)
    {
        const auto &hero = hero_data.hero_vec.at(hero_idx_zero_based);

        if (HeroCastSkillIfAvailable(hero, skill_id, hero_conditions, target_logic, current_target_id))
        {
#ifdef _DEBUG
            Log::Info("Casted %s.", skill_name);
#else
            (void)skill_name;
#endif
            return true;
        }
    }

    return true;
}

bool PlayerHasHerosInParty()
{
    const auto me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *const party_info = GW::PartyMgr::GetPartyInfo();
    if (!party_info || !party_info->heroes.valid())
        return false;


    for (const auto &hero : party_info->heroes)
    {
        if (!hero.agent_id)
            continue;

        const auto hero_agent = GW::Agents::GetAgentByID(hero.agent_id);
        if (!hero_agent)
            continue;

        const auto hero_living = hero_agent->GetAsAgentLiving();
        if (!hero_living)
            continue;

        if (hero_living->login_number == me_living->agent_id)
            return true;
    }

    return false;
}
