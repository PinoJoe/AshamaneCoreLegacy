/*
* Copyright (C) 2017-2018 AshamaneProject <https://github.com/AshamaneProject>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "AreaTrigger.h"
#include "AreaTriggerAI.h"
#include "AreaTriggerTemplate.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "tomb_of_sargeras.h"

enum Spells
{
    SPELL_BURNING_ARMOR                 = 231363,
    SPELL_BURNING_ERUPTION              = 231395, // Casted at SPELL_BURNING_ARMOR end

    SPELL_CRASHING_COMET                = 232249,
    SPELL_CRASHING_COMET_DAMAGE         = 230345,

    SPELL_INFERNAL_SPIKE_SUMMON         = 233055,
    SPELL_INFERNAL_SPIKE                = 233019,

    SPELL_SHATTERING_STAR               = 233272,
    SPELL_SHATTERING_STAR_AT            = 233279,
    SPELL_SHATTERING_STAR_DAMAGE        = 233281,
    SPELL_SHATTERING_STAR_FINAL_DAMAGE  = 233283,
    
    SPELL_INFERNAL_BURNING              = 233062,
    SPELL_INFERNAL_BURNING_REMOVE_SPIKES= 233078,
};

enum Misc
{
    NPC_INFERNAL_SPIKE                  = 116976,

    SPELLVISUAL_INFERNAL_SPIKE_DESTROY  = 66119,

    AT_SHATTERING_STAR                  = 13412,
};

struct boss_goroth : public BossAI
{
    boss_goroth(Creature* creature) : BossAI(creature, DATA_GOROTH) { }

    void EnterCombat(Unit* /*attacker*/) override
    {
        events.ScheduleEvent(SPELL_BURNING_ARMOR, 16s);
        events.ScheduleEvent(SPELL_CRASHING_COMET, 10s, 20s);
        events.ScheduleEvent(SPELL_INFERNAL_SPIKE_SUMMON, 12s);
        events.ScheduleEvent(SPELL_SHATTERING_STAR, 10s, 20s);
        events.ScheduleEvent(SPELL_INFERNAL_BURNING, 55s);
    }

    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case SPELL_BURNING_ARMOR:
            {
                if (Unit* target = SelectTarget(SELECT_TARGET_TOPAGGRO))
                    DoCast(target, SPELL_BURNING_ARMOR);

                events.Repeat(16s);
                break;
            }
            case SPELL_CRASHING_COMET:
            {
                UnitList targetList;
                SelectTargetList(targetList, 3, SELECT_TARGET_RANDOM, 200.0f, true);

                for (Unit* target : targetList)
                    DoCast(target, SPELL_CRASHING_COMET, true);

                events.Repeat(10s, 20s);
                break;
            }
            case SPELL_INFERNAL_SPIKE_SUMMON:
            {
                UnitList targetList;
                SelectTargetList(targetList, 3, SELECT_TARGET_RANDOM, 200.0f, true);

                for (Unit* target : targetList)
                    DoCast(target, SPELL_INFERNAL_SPIKE_SUMMON);

                events.Repeat(12s);
                break;
            }
            case SPELL_SHATTERING_STAR:
            {
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 2))
                {
                    _shatteringStarTargetGUID = target->GetGUID();
                    DoCast(target, SPELL_SHATTERING_STAR);
                }

                events.Repeat(10s, 20s);
                break;
            }
            case SPELL_INFERNAL_BURNING:
            {
                DoCast(SPELL_INFERNAL_BURNING);
                events.Repeat(55s);
                break;
            }
            default:
                break;
        }
    }

    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);

        if (summon->GetEntry() == NPC_INFERNAL_SPIKE)
            summon->CastSpell(summon, SPELL_INFERNAL_SPIKE, false);
    }

    void JustRegisteredAreaTrigger(AreaTrigger* at) override
    {
        if (at->GetTemplate()->Id == AT_SHATTERING_STAR)
        {
            if (Unit* target = ObjectAccessor::GetUnit(*me, _shatteringStarTargetGUID))
                at->SetDestination(*target, 1000);

            _shatteringStarTargetGUID = ObjectGuid::Empty;
        }
    }

private:
    ObjectGuid _shatteringStarTargetGUID;
};

// 231363
class spell_burning_armor : public AuraScript
{
    PrepareAuraScript(spell_burning_armor);

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(nullptr, SPELL_BURNING_ERUPTION, true);
    }

    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(spell_burning_armor::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 230345
class spell_crashing_comet_damage : public SpellScript
{
    PrepareSpellScript(spell_crashing_comet_damage);

    void HandleHit(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        if (!target)
            return;

        CreatureList infernalSpikes;
        target->GetCreatureListWithEntryInGrid(infernalSpikes, NPC_INFERNAL_SPIKE, GetSpellInfo()->GetMaxRange());

        for (Creature* infernalSpike : infernalSpikes)
        {
            infernalSpike->SendPlaySpellVisual(infernalSpike->GetGUID(), SPELLVISUAL_INFERNAL_SPIKE_DESTROY);
            infernalSpike->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_crashing_comet_damage::HandleHit, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// 233062
class spell_infernal_burning : public SpellScript
{
    PrepareSpellScript(spell_infernal_burning);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        CreatureList infernalSpikes;
        GetCaster()->GetCreatureListWithEntryInGrid(infernalSpikes, NPC_INFERNAL_SPIKE, GetSpellInfo()->GetMaxRange());

        targets.remove_if([this, infernalSpikes] (WorldObject* worldObject)
        {
            for (Creature* spike : infernalSpikes)
                if (spike->IsInBetween(GetCaster(), worldObject))
                    return true;

            return false;
        });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_infernal_burning::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_infernal_burning::FilterTargets, EFFECT_1, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// 233078
class spell_infernal_burning_remove_spikes : public SpellScript
{
    PrepareSpellScript(spell_infernal_burning_remove_spikes);

    void HandleOnCast()
    {
        CreatureList infernalSpikes;
        GetCaster()->GetCreatureListWithEntryInGrid(infernalSpikes, NPC_INFERNAL_SPIKE, GetSpellInfo()->GetMaxRange());

        for (Creature* infernalSpike : infernalSpikes)
        {
            infernalSpike->SendPlaySpellVisual(GetCaster()->GetGUID(), SPELLVISUAL_INFERNAL_SPIKE_DESTROY);
            infernalSpike->DespawnOrUnsummon();
        }
    }

    void Register() override
    {
        OnCast += SpellCastFn(spell_infernal_burning_remove_spikes::HandleOnCast);
    }
};

//AT : 13412
//Spell : 233279
struct at_goroth_shattering_star : AreaTriggerAI
{
    at_goroth_shattering_star(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger) { }

    void OnUnitEnter(Unit* unit) override
    {
        if (unit->GetEntry() == NPC_INFERNAL_SPIKE)
            ++_infernalSpikeTouched;
        else if (Unit* caster = at->GetCaster())
            caster->CastSpell(unit, SPELL_SHATTERING_STAR_DAMAGE, true);
    }

    void OnDestinationReached() override
    {
        if (Unit* caster = at->GetCaster())
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_SHATTERING_STAR_FINAL_DAMAGE))
                if (SpellEffectInfo const* spellEffectInfo = spellInfo->GetEffect(caster->GetMap()->GetDifficultyID(), EFFECT_0))
                    caster->CastCustomSpell(SPELL_SHATTERING_STAR_FINAL_DAMAGE, SPELLVALUE_BASE_POINT0, spellEffectInfo->BasePoints / std::max(_infernalSpikeTouched, uint8(1)), nullptr, TRIGGERED_FULL_MASK);

        // TODO : Deal damage to target if _infernalSpikeTouched == 0
    }

    uint8 _infernalSpikeTouched = 0;
};

void AddSC_boss_goroth()
{
    RegisterCreatureAI(boss_goroth);

    RegisterAuraScript(spell_burning_armor);
    RegisterSpellScript(spell_crashing_comet_damage);
    RegisterSpellScript(spell_infernal_burning);
    RegisterSpellScript(spell_infernal_burning_remove_spikes);

    RegisterAreaTriggerAI(at_goroth_shattering_star);
}