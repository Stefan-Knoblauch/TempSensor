/*
 * gen_vectors.c - Writes payload test vectors (JSON) produced by the real
 * encoders, for cross-checking an independent (e.g. Python) decoder.
 *
 * Usage: gen_vectors <output.json>
 *
 * Format: [ {"name": "...", "hex": "...", "fields": {...}}, ... ]
 * Field names follow the payload spec in snake_case; temperatures are raw
 * int16 centi-degC (-32768 = 0x8000 = not available).
 */
#include <stdio.h>

#include "payload.h"

#define T_BASE 1767225600u /* 2026-01-01T00:00:00Z */

static FILE *g_out;
static int g_count;

static void begin(const char *name, const uint8_t *buf, size_t len)
{
    size_t i;
    fprintf(g_out, "%s  {\n    \"name\": \"%s\",\n    \"hex\": \"", g_count ? ",\n" : "", name);
    for (i = 0; i < len; i++) {
        fprintf(g_out, "%02x", buf[i]);
    }
    fprintf(g_out, "\",\n    \"fields\": {");
    g_count++;
}

static void end(void)
{
    fprintf(g_out, "}\n  }");
}

static void rec_json(const pl_record_t *r)
{
    fprintf(g_out, "{\"rail_cC\": %d, \"air_cC\": %d, \"flags\": %u}", (int)r->rail_cC,
            (int)r->air_cC, (unsigned)r->flags);
}

static void batch(const char *name, uint16_t seq, uint32_t base, const pl_record_t *recs,
                  uint8_t n)
{
    uint8_t buf[PL_BATCH_SIZE(48)];
    size_t len = pl_encode_batch(buf, sizeof buf, seq, base, 10, recs, n);
    uint8_t i;
    begin(name, buf, len);
    fprintf(g_out,
            "\"type\": 1, \"seq\": %u, \"base_time\": %lu, \"interval_min\": 10, \"n\": %u, "
            "\"records\": [",
            (unsigned)seq, (unsigned long)base, (unsigned)n);
    for (i = 0; i < n; i++) {
        if (i) fprintf(g_out, ", ");
        rec_json(&recs[i]);
    }
    fprintf(g_out, "]");
    end();
}

static void alarm(const char *name, uint16_t seq, uint32_t t, uint8_t code,
                  const pl_record_t *r)
{
    uint8_t buf[PL_ALARM_SIZE];
    size_t len = pl_encode_alarm(buf, sizeof buf, seq, t, code, r);
    begin(name, buf, len);
    fprintf(g_out, "\"type\": 2, \"seq\": %u, \"time\": %lu, \"code\": %u, \"record\": ",
            (unsigned)seq, (unsigned long)t, (unsigned)code);
    rec_json(r);
    end();
}

int main(int argc, char **argv)
{
    /* Flags: attached + lid closed = 0x03 in normal running. */
    const uint8_t OK = PL_FLAG_ATTACHED | PL_FLAG_LID_CLOSED;
    const pl_record_t six[6] = {
        { 2875, 2210, OK },
        { 3012, 2290, OK },
        { 3188, 2365, OK },
        { PL_TEMP_NA, 2410, OK | PL_FLAG_FAULT }, /* rail sensor fault */
        { 3420, 2455, OK },
        { 3561, 2490, (uint8_t)(OK | PL_FLAG_HOT) },
    };
    const pl_record_t two[2] = { { -150, -420, OK }, { -95, -380, OK | PL_FLAG_LOW_BATT } };
    const pl_record_t a_up = { 4612, 3105,
                               pl_make_flags(true, true, true, 2, false, false) };
    const pl_record_t a_det = { 3302, PL_TEMP_NA, pl_make_flags(false, true, false, 0, false, true) };
    uint8_t buf[32];
    size_t len;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <output.json>\n", argv[0]);
        return 2;
    }
    g_out = fopen(argv[1], "w");
    if (!g_out) {
        perror(argv[1]);
        return 1;
    }
    fprintf(g_out, "[\n");

    batch("batch_ltem_n6_with_fault", 0x0102, T_BASE, six, 6);
    batch("batch_lora_n2_negative", 0xFFFF, T_BASE + 600u, two, 2);
    alarm("alarm_level_up_l2", 0x0007, T_BASE + 3000u, PL_ALM_LEVEL_UP, &a_up);
    alarm("alarm_detached_air_fault", 0x8001, T_BASE + 3123u, PL_ALM_DETACHED, &a_det);

    {
        const pl_status_t s = { 0x0123, T_BASE + 86400u, 87, 1234, 0x0102, 3, 5, 200 };
        len = pl_encode_status(buf, sizeof buf, &s);
        begin("status_heartbeat", buf, len);
        fprintf(g_out,
                "\"type\": 3, \"seq\": %u, \"time\": %lu, \"battery_pct\": %u, "
                "\"energy_mAh\": %u, \"fw_version\": %u, \"reset_count\": %u, "
                "\"tx_fail_count\": %u, \"link_quality\": %u",
                (unsigned)s.seq, (unsigned long)s.time, (unsigned)s.battery_pct,
                (unsigned)s.energy_mAh, (unsigned)s.fw_version, (unsigned)s.reset_count,
                (unsigned)s.tx_fail_count, (unsigned)s.link_quality);
        end();
    }
    {
        const pl_install_t s = { 0x0000, T_BASE - 60u, 2, 0x0102, 1, 100 };
        len = pl_encode_install(buf, sizeof buf, &s);
        begin("install", buf, len);
        fprintf(g_out,
                "\"type\": 4, \"seq\": %u, \"time\": %lu, \"hw_rev\": %u, \"fw_version\": %u, "
                "\"attached\": %u, \"battery_pct\": %u",
                (unsigned)s.seq, (unsigned long)s.time, (unsigned)s.hw_rev,
                (unsigned)s.fw_version, (unsigned)s.attached, (unsigned)s.battery_pct);
        end();
    }
    {
        /* Downlink 0x81, 18 B (D-11). */
        pl_config_t c;
        c.l1 = 5500;
        c.l2 = 6000;
        c.l3 = 6500;
        c.hot_threshold = 3500;
        c.rate_of_rise = 800;
        c.hysteresis = 100;
        c.batch_period_min = 60;
        c.cool_threshold = 2500;
        c.cool_batch_min = 240;
        c.hot_batch_min = 20;
        len = pl_encode_config(buf, sizeof buf, &c);
        begin("config_downlink", buf, len);
        fprintf(g_out,
                "\"type\": 129, \"l1\": %d, \"l2\": %d, \"l3\": %d, \"hot_threshold\": %d, "
                "\"rate_of_rise\": %u, \"hysteresis\": %u, \"batch_period_min\": %u, "
                "\"cool_threshold\": %d, \"cool_batch_min\": %u, \"hot_batch_min\": %u",
                c.l1, c.l2, c.l3, c.hot_threshold, (unsigned)c.rate_of_rise,
                (unsigned)c.hysteresis, (unsigned)c.batch_period_min, c.cool_threshold,
                (unsigned)c.cool_batch_min, (unsigned)c.hot_batch_min);
        end();
    }

    fprintf(g_out, "\n]\n");
    if (fclose(g_out) != 0) {
        perror(argv[1]);
        return 1;
    }
    printf("wrote %d vectors to %s\n", g_count, argv[1]);
    return 0;
}
