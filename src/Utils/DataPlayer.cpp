#include <utility>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Context/ItemContext.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/MemoryMgr.h>
#include <GWCA/Managers/PartyMgr.h>

#include "ActionsBase.h"
#include "DataPlayer.h"
#include "DataSkillbar.h"
#include "Helper.h"
#include "HelperAgents.h"
#include "Utils.h"

namespace
{
DWORD GetTimeElapsed(const DWORD timestamp)
{
    return GW::MemoryMgr::GetSkillTimer() - timestamp;
}

float GetTimeRemaining(const float duration, const DWORD timestamp)
{
    return duration * 1000.0F - static_cast<float>(GetTimeElapsed(timestamp));
}
} // namespace

bool DataPlayer::ValidateData(std::function<bool(bool)> cb_fn, const bool need_party_loaded) const
{
    if (!cb_fn(need_party_loaded))
        return false;

    const auto *const me_agent = GW::Agents::GetPlayer();
    const auto *const me_living = GW::Agents::GetPlayerAsAgentLiving();

    return (me_agent != nullptr && me_living != nullptr && me_agent->agent_id != 0 && me_living->agent_id != 0);
}

void DataPlayer::Update()
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return;

    id = me_living->agent_id;
    pos = me_living->pos;
    me_living = me_living;

    target = GW::Agents::GetTarget();

    const auto energy_tpl = GetEnergy(me_living);
    energy = std::get<0>(energy_tpl);
    max_energy = std::get<1>(energy_tpl);
    energy_perc = std::get<2>(energy_tpl);

    const auto hp_tpl = GetHp(me_living);
    hp = std::get<0>(hp_tpl);
    max_hp = std::get<1>(hp_tpl);
    hp_perc = std::get<2>(hp_tpl);

    primary = static_cast<GW::Constants::Profession>(me_living->primary);
    secondary = static_cast<GW::Constants::Profession>(me_living->secondary);

    const auto equipped_items_bag = GW::Items::GetBag(GW::Constants::Bag::Equipped_Items);
    if (!equipped_items_bag)
        return;
    weapon_main_hand = GW::Items::GetItemBySlot(equipped_items_bag, 1);

    holds_melee_weapon = weapon_main_hand && (weapon_main_hand->type == GW::Constants::ItemType::Axe ||
                                              weapon_main_hand->type == GW::Constants::ItemType::Hammer ||
                                              weapon_main_hand->type == GW::Constants::ItemType::Sword ||
                                              weapon_main_hand->type == GW::Constants::ItemType::Daggers ||
                                              weapon_main_hand->type == GW::Constants::ItemType::Scythe ||
                                              weapon_main_hand->type == GW::Constants::ItemType::Spear);
    holds_caster_weapon = !holds_melee_weapon;

    is_melee_class = primary == GW::Constants::Profession::Assassin || primary == GW::Constants::Profession::Dervish ||
                     primary == GW::Constants::Profession::Warrior || primary == GW::Constants::Profession::Paragon ||
                     primary == GW::Constants::Profession::Ranger;
    is_caster_class = !is_melee_class;

    static auto standing_timer_start_ms = clock();
    if (me_living->GetIsMoving())
        standing_timer_start_ms = clock();

    standing_for_ms = TIMER_DIFF(standing_timer_start_ms);
}

bool DataPlayer::CanCast() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    if (!me_living)
        return false;

    if (me_living->GetIsDead() || me_living->GetIsKnockedDown() || me_living->GetIsCasting() ||
        me_living->GetIsMoving())
        return false;

    return true;
}

bool DataPlayer::CanAttack() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    if (!me_living)
        return false;

    if (me_living->GetIsDead() || me_living->GetIsKnockedDown() || me_living->GetIsCasting() ||
        me_living->GetIsMoving())
        return false;

    return true;
}

bool DataPlayer::IsAttacking() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    return me_living->GetIsAttacking();
}

bool DataPlayer::IsCasting() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    return me_living->GetIsCasting();
}

bool DataPlayer::IsFighting() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    if (IsAttacking() || IsCasting())
        return true;

    return standing_for_ms > 2'000 && standing_for_ms < 20'000; // Assuming standing in enemies
}

bool DataPlayer::IsMoving() const
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    return me_living->GetIsMoving();
}

void DataPlayer::ChangeTarget(const uint32_t target_id)
{
    if (!target_id || !GW::Agents::GetAgentByID(target_id))
        return;

    GW::GameThread::Enqueue([&, target_id] {
        GW::Agents::ChangeTarget(target_id);
        target = GW::Agents::GetTarget();
    });
}

bool DataPlayer::HasBuff(const GW::Constants::SkillID buff_skill_id)
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *const me_buffs = GW::Effects::GetPlayerBuffs();
    if (!me_buffs || !me_buffs->valid())
        return false;

    for (const auto &buff : *me_buffs)
    {
        const auto agent_id = buff.target_agent_id;
        const auto skill_id = buff.skill_id;

        if (agent_id == me_living->agent_id && skill_id == buff_skill_id)
            return true;
    }

    return false;
}

bool DataPlayer::CastEffectIfNotAvailable(const DataSkill &skill_data) const
{
    const auto has_bond = HasEffect(static_cast<GW::Constants::SkillID>(skill_data.id));
    const auto bond_avail = skill_data.CanBeCasted(energy);

    if (!has_bond && bond_avail)
    {
        GW::GameThread::Enqueue([&]() { GW::SkillbarMgr::UseSkill(skill_data.idx, id); });
        return true;
    }

    return false;
}

bool DataPlayer::CastEffect(const DataSkill &skill_data) const
{
    if (skill_data.CanBeCasted(energy))
    {
        GW::GameThread::Enqueue([&]() { GW::SkillbarMgr::UseSkill(skill_data.idx, id); });
        return true;
    }

    return false;
}

/* START STATIC FUNCTIONS */

bool DataPlayer::HasEffect(const GW::Constants::SkillID effect_skill_id)
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *const effects = GW::Effects::GetPlayerEffects();
    if (!effects)
        return false;

    for (const auto &effect : *effects)
    {
        const auto agent_id = effect.agent_id;
        const auto skill_id = effect.skill_id;

        if ((agent_id == me_living->agent_id || agent_id == 0) && (skill_id == effect_skill_id))
            return true;
    }

    return false;
}

uint32_t DataPlayer::GetNumberOfPartyBonds()
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *player_buffs = GW::Effects::GetPlayerBuffs();
    if (!player_buffs || !player_buffs->valid())
        return false;

    auto num_bonds = uint32_t{0};
    for (const auto &buff : *player_buffs)
    {
        const auto agent_id = buff.target_agent_id;

        if (agent_id != me_living->agent_id)
            ++num_bonds;
    }

    return num_bonds;
}

float DataPlayer::GetRemainingEffectDuration(const GW::Constants::SkillID effect_skill_id)
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *me_effects = GW::Effects::GetPlayerEffectsArray();
    if (!me_effects)
        return false;

    for (const auto &effect : me_effects->effects)
    {
        const auto agent_id = effect.agent_id;
        const auto skill_id = effect.skill_id;

        if (agent_id == me_living->agent_id || agent_id == 0)
        {
            if (skill_id == effect_skill_id)
            {
                return GetTimeRemaining(effect.duration, effect.timestamp);
            }
        }
    }

    return 0.0F;
}

bool DataPlayer::PlayerOrHeroHasEffect(const GW::Constants::SkillID effect_id)
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *effects = GetEffects(me_living->agent_id);
    if (!effects)
        return false;

    for (const auto effect : *effects)
    {
        if (effect.skill_id == effect_id)
            return true;
    }

    return false;
}

bool DataPlayer::PlayerHasEffect(const GW::Constants::SkillID effect_id, const bool ignore_id)
{
    const auto *me_living = GW::Agents::GetPlayerAsAgentLiving();
    if (!me_living)
        return false;

    const auto *player_effects = GW::Effects::GetPlayerEffects();
    if (!player_effects)
        return false;

    for (const auto &effect : *player_effects)
    {
        if (effect.skill_id == effect_id && (ignore_id || effect.agent_id == me_living->agent_id))
            return true;
    }

    return false;
}
