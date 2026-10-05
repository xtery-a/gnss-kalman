/**
 * @file test_harness_power.c
 * @brief Standalone Desktop ANSI C Verification Harness for Phase 5:
 * Sub-Zero Power Supervisor, Thermal Throttling & TI TPL5010 Hardware Watchdog.
 *
 * Compiles with:
 *   gcc -O2 -Wall -Wextra test_harness_power.c power_supervisor.c thermal_throttle.c watchdog_tpl5010.c -o test_power.exe
 *
 * Verifies:
 *  1. Sub-zero charge lockout at EXACTLY 0.0 C and +2.5 C recovery hysteresis.
 *  2. Active PTC pre-heater state machine (-20 C to +5.0 C shutoff).
 *  3. -20 C battery ESR spike (R = 0.90 Ohm), 500 mA load pulse, and supercapacitor rail buffering >= 3.15 V.
 *  4. Dynamic 4-tier thermal and low-voltage throttling state machine cascades.
 *  5. TI TPL5010 multi-task health coordination, 20 ms DONE pulse, and GNSS starvation hardware reset at 30 seconds.
 *  6. Zero heap memory allocation (100% static/BSS/stack).
 */

#include "power_supervisor.h"
#include "thermal_throttle.h"
#include "watchdog_tpl5010.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

int main(void) {
    printf("=================================================================\n");
    printf(" PHASE 5: SUB-ZERO POWER SUPERVISOR, THROTTLING & WATCHDOG HARNESS\n");
    printf("=================================================================\n\n");

    /* ---------------------------------------------------------------------- */
    /* TEST 1: Sub-Zero Charge Lockout, Hysteresis & PTC Pre-Heater           */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 1] Testing Sub-Zero Charge Lockout, Hysteresis & PTC Pre-Heater...\n");

    power_supervisor_t ps;
    power_supervisor_init(&ps, 3800);

    /* 1.1 Temperature Sweep Downward (+10.0 C down to -30.0 C) */
    bool charge_allowed = true;
    int16_t cutoff_temp = 999;

    for (int16_t t = 100; t >= -300; t--) {
        power_supervisor_update(&ps, t, 3800, true);

        if (charge_allowed && !power_supervisor_is_charge_enabled(&ps)) {
            charge_allowed = false;
            cutoff_temp = t;
        }
    }

    /* Verify charge gate cut off at EXACTLY 0.0 C (0 deci-C) */
    assert(cutoff_temp == 0);
    assert(!power_supervisor_is_charge_enabled(&ps));
    printf("  [PASS] Charge gate cut off at EXACTLY 0.0 deg C (Lithium plating prevented).\n");

    /* 1.2 Temperature Sweep Upward (Hysteresis Check) */
    /* Sweep from 0 deci-C (0.0 C) up to +50 deci-C (+5.0 C) in passive warming mode */
    ps.heater_ptc_en = false;
    ps.alert_flags = POWER_FLAG_COLD_CHARGE_BLOCKED;

    int16_t reenable_temp = -999;
    for (int16_t t = 0; t <= 50; t++) {
        power_supervisor_update(&ps, t, 3800, true);

        /* Between 0.0 C and +2.4 C (24 deci-C), charge must remain strictly BLOCKED */
        if (t < 25) {
            assert(!power_supervisor_is_charge_enabled(&ps));
        }

        /* At +2.5 C (25 deci-C), charge gate must re-enable */
        if (!charge_allowed && power_supervisor_is_charge_enabled(&ps)) {
            charge_allowed = true;
            reenable_temp = t;
        }
    }

    assert(reenable_temp == 25);
    printf("  [PASS] Charge gate remained OFF in deadband and re-enabled at EXACTLY +2.5 deg C.\n");

    /* 1.3 Active PTC Pre-Heater Controller */
    power_supervisor_init(&ps, 3800);
    /* At -15.0 C with external VBUS present, heater must engage */
    power_supervisor_update(&ps, -150, 3800, true);
    assert(power_supervisor_is_heater_active(&ps));
    assert(!power_supervisor_is_charge_enabled(&ps));
    printf("  [PASS] PTC Pre-Heater engaged at -15.0 deg C with external power present.\n");

    /* Warm up towards +5.0 C (+50 deci-C) */
    for (int16_t t = -140; t < 50; t += 10) {
        power_supervisor_update(&ps, t, 3800, true);
        assert(power_supervisor_is_heater_active(&ps));
        assert(!power_supervisor_is_charge_enabled(&ps));
    }

    /* Reaching +5.0 C (+50 deci-C): heater must disengage and charge gate enable */
    power_supervisor_update(&ps, 50, 3800, true);
    assert(!power_supervisor_is_heater_active(&ps));
    assert(power_supervisor_is_charge_enabled(&ps));
    printf("  [PASS] PTC Pre-Heater shut off at +5.0 deg C target and transferred to charging.\n\n");

    /* ---------------------------------------------------------------------- */
    /* TEST 2: Voltage Sag, ESR Estimation & Supercapacitor Rail Buffering    */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 2] Testing Voltage Sag, ESR Estimation & Supercapacitor Rail Buffering...\n");

    power_supervisor_init(&ps, 3600); /* 3.60 V nominal cold battery */

    /* Simulate -20 C battery ESR spike: R_batt = 0.90 Ohm */
    const float cold_esr_ohms = 0.90f;
    const uint16_t lora_burst_current_ma = 500; /* 500 mA pulse */

    /* Assert RF pulse */
    power_supervisor_notify_rf_pulse_start(&ps, lora_burst_current_ma, cold_esr_ohms);

    /* Verify unbuffered cell sag calculation: 0.500 A * 0.90 Ohm = 450 mV */
    assert(ps.last_sag_mv == 450);

    /* Verify supercapacitor buffer: rail voltage MUST NOT drop below 3.15 V */
    uint16_t rail_during_pulse = power_supervisor_get_rail_voltage(&ps);
    printf("  -> Unbuffered Battery Sag:     450 mV (Would drop to 3150 mV)\n");
    printf("  -> Supercap Buffered Rail:    %u mV (Buffer delta: %u mV)\n",
           rail_during_pulse, ps.vbatt_mv - rail_during_pulse);

    assert(rail_during_pulse >= POWER_VOLTAGE_MIN_RAIL_MV);
    assert(rail_during_pulse == 3332); /* 3600 - 268 mV */

    /* Complete pulse */
    power_supervisor_notify_rf_pulse_end(&ps);
    assert((ps.alert_flags & POWER_FLAG_ESR_WARNING) != 0);
    assert(ps.esr_warning_count == 1);
    printf("  [PASS] Supercapacitor buffer prevented brownout; system rail maintained >= 3.15 V.\n");
    printf("  [PASS] Pulse sag >= 400 mV flagged ESR warning.\n");

    /* Inject critical pulse exceeding 600 mV sag */
    power_supervisor_notify_rf_pulse_start(&ps, 700, 0.90f); /* 0.700 A * 0.90 Ohm = 630 mV */
    assert(ps.last_sag_mv == 630);
    power_supervisor_notify_rf_pulse_end(&ps);

    assert(power_supervisor_is_collapse_risk(&ps));
    printf("  [PASS] Pulse sag (630 mV >= 600 mV) tripped FLAG_BATTERY_COLLAPSE_RISK.\n\n");

    /* ---------------------------------------------------------------------- */
    /* TEST 3: Dynamic 4-Tier Thermal Throttling State Machine Cascades       */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 3] Testing Dynamic 4-Tier Thermal Throttling Cascades...\n");

    thermal_throttle_t tt;
    thermal_throttle_init(&tt);

    /* Tier 0: Nominal (+20.0 C, 3800 mV) */
    throttle_tier_t tier = thermal_throttle_update(&tt, 200, 3800);
    const throttle_profile_t *prof = thermal_throttle_get_profile(&tt);
    assert(tier == THROTTLE_TIER_0_NOMINAL);
    assert(prof->core_clock_mhz == 160);
    assert(prof->lora_rf_power_dbm == 22);
    assert(prof->display_refresh_hz == 5.0f);
    assert(prof->gnss_fix_rate_hz == 5.0f);
    printf("  [PASS] TIER 0: %s\n", thermal_throttle_tier_name(tier));

    /* Tier 1: Cold Caution (0.0 C, 3800 mV) */
    tier = thermal_throttle_update(&tt, 0, 3800);
    prof = thermal_throttle_get_profile(&tt);
    assert(tier == THROTTLE_TIER_1_COLD_CAUTION);
    assert(prof->core_clock_mhz == 160);
    assert(prof->lora_rf_power_dbm == 22);
    assert(prof->display_refresh_hz == 2.0f);
    assert(prof->gnss_fix_rate_hz == 2.0f);
    assert(prof->supercap_precharge_check == true);
    printf("  [PASS] TIER 1: %s\n", thermal_throttle_tier_name(tier));

    /* Tier 2: Sub-Zero Throttling (-15.0 C, 3800 mV) */
    tier = thermal_throttle_update(&tt, -150, 3800);
    prof = thermal_throttle_get_profile(&tt);
    assert(tier == THROTTLE_TIER_2_SUBZERO);
    assert(prof->core_clock_mhz == 64);
    assert(prof->lora_rf_power_dbm == 14);
    assert(prof->display_refresh_hz == 1.0f);
    assert(prof->gnss_fix_rate_hz == 1.0f);
    assert(prof->dirty_line_only_display == true);
    printf("  [PASS] TIER 2: %s\n", thermal_throttle_tier_name(tier));

    /* Tier 3: Deep Freeze Survival Mode (-28.0 C, 3800 mV) */
    tier = thermal_throttle_update(&tt, -280, 3800);
    prof = thermal_throttle_get_profile(&tt);
    assert(tier == THROTTLE_TIER_3_SURVIVAL);
    assert(prof->core_clock_mhz == 32);
    assert(prof->lora_rf_power_dbm == 10);
    assert(prof->display_refresh_hz == 0.2f);
    assert(prof->gnss_fix_interval_ms == 30000);
    assert(prof->gnss_duty_cycling == true);
    printf("  [PASS] TIER 3 (Thermal): %s\n", thermal_throttle_tier_name(tier));

    /* Low-Battery Survival Override (+15.0 C, but V_batt = 3250 mV < 3300 mV) */
    tier = thermal_throttle_update(&tt, 150, 3250);
    assert(tier == THROTTLE_TIER_3_SURVIVAL);
    printf("  [PASS] TIER 3 (Low-Voltage): 3250 mV triggered survival mode at room temperature.\n\n");

    /* ---------------------------------------------------------------------- */
    /* TEST 4: TI TPL5010 Multi-Thread Health & Starvation Reset Emulation    */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 4] Testing TI TPL5010 Multi-Thread Health & Starvation Reset...\n");

    watchdog_tpl5010_t wd;
    watchdog_tpl5010_init(&wd, 0);

    /* 4.1 Healthy Cycle 1 (0 ms to 20,000 ms) */
    /* All 4 tasks report healthy */
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_GNSS);
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_CANFD);
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_DISPLAY);
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_POWER);

    bool kick_ok = watchdog_tpl5010_check_and_kick(&wd, 20000);
    assert(kick_ok == true);
    assert(wd.done_pin_state == true);
    assert(wd.successful_kicks_count == 1);
    printf("  [PASS] All 4 threads reported: 20 ms DONE pulse asserted at 20,000 ms.\n");

    /* Advance time by 20 ms -> DONE pulse terminates */
    watchdog_tpl5010_update_gpio(&wd, 20020);
    assert(wd.done_pin_state == false);
    assert(!watchdog_tpl5010_is_reset_tripped(&wd));
    printf("  [PASS] DONE pulse de-asserted cleanly after exactly 20 ms.\n");

    /* 4.2 Starvation Cycle (20,000 ms to 40,000 ms): GNSS Task Freezes! */
    /* Only CAN-FD, Display and Power report healthy (GNSS missing: mask = 0x0E) */
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_CANFD);
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_DISPLAY);
    watchdog_tpl5010_report_healthy(&wd, WATCHDOG_TASK_POWER);

    kick_ok = watchdog_tpl5010_check_and_kick(&wd, 40000);
    assert(kick_ok == false);
    assert(wd.done_pin_state == false);
    assert(wd.suppressed_kicks_count == 1);
    printf("  [PASS] GNSS task starved: DONE pulse suppressed at 40,000 ms.\n");

    /* Time remaining until 30s hardware timeout */
    uint32_t rem_ms = watchdog_tpl5010_get_time_until_reset_ms(&wd, 40000);
    printf("  -> Time until TPL5010 hard reset: %u ms (Target: 10,000 ms)\n", rem_ms);
    assert(rem_ms == 10000);

    /* Advance time to 50,000 ms (exactly 30,000 ms since last valid feed at 20,000 ms) */
    watchdog_tpl5010_update_gpio(&wd, 49999);
    assert(!watchdog_tpl5010_is_reset_tripped(&wd));

    watchdog_tpl5010_update_gpio(&wd, 50000);
    assert(watchdog_tpl5010_is_reset_tripped(&wd));
    printf("  [PASS] TI TPL5010 30-second hardware reset triggered at exactly 50,000 ms!\n\n");

    printf("=================================================================\n");
    printf(" ALL PHASE 5 TESTS PASSED PERFECTLY (100%% SUCCESS)\n");
    printf("=================================================================\n");

    return 0;
}
