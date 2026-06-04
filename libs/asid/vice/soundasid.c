/*
 * soundasid.c - VICE adapter: ASID-over-MIDI sound device.
 *
 * The protocol codec (SID register tracking + SysEx update encoding) lives in
 * the revice asid core (libs/asid) and is unit-tested there. This file is the
 * VICE/ALSA half: it enumerates and opens a MIDI port, drives the encoded
 * messages onto it, registers the VICE sound_device_t, and translates the
 * emulator's per-write dump callback (with IRQ-relative timing) into core
 * calls.
 *
 * Example usage:
 *
 *    x64 -sounddev asid -soundarg 1
 *
 * Originally written by aTc <aTc@k-n-p.org>, updated by josh@vandervecken.com.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "debug.h"
#include "log.h"
#include "vice.h"

#include "interrupt.h"
#include "maincpu.h"

#include "sound.h"
#include "types.h"

#include "revice_asid.h"

#include <alsa/asoundlib.h>

#define ALL_MIDI_PORTS -1
#define NO_PORT -1

static snd_seq_t *seq;
static int vport, queue_id;
static snd_seq_port_subscribe_t *subscription;
static snd_midi_event_t *coder;

static asid_core_t asid;
static uint32_t bytes_total = 0;

/* Per-chip realtime flush timing (emulator IRQ clock + host wall clock). */
static struct {
    CLOCK    last_irq;
    uint64_t start_clock;
} asid_timing[ASID_CHIPS];

static uint64_t get_clock(void) {
  struct timespec res;
  clock_gettime(CLOCK_MONOTONIC, &res);
  return (res.tv_sec * 1e9) + res.tv_nsec;
}

static int _send_message(const uint8_t *message, uint8_t message_len,
                         uint64_t nsec);

/* TODO: refactor libmididrv API for cross platform support. */
static int _initialize_midi(void) {
  int result =
      snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK);
  if (result < 0) {
    log_message(LOG_DEFAULT, "snd_seq_open() failed");
    return -1;
  }

  snd_seq_set_client_name(seq, "asid");

  vport = NO_PORT;
  coder = 0;
  result = snd_midi_event_new(ASID_BUFFER_SIZE, &coder);
  if (result < 0) {
    log_message(LOG_DEFAULT, "snd_midi_event_new() failed");
    return -1;
  }
  snd_midi_event_init(coder);
  snd_seq_set_client_pool_output(seq, 65536);
  return 0;
}

static unsigned int _get_port_info(snd_seq_port_info_t *pinfo,
                                   int port_number) {
  unsigned int port_type = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
  snd_seq_client_info_t *cinfo;
  int client;
  int count = 0;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_client_info_set_client(cinfo, -1);

  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    client = snd_seq_client_info_get_client(cinfo);
    if (client) {
      snd_seq_port_info_set_client(pinfo, client);
      snd_seq_port_info_set_port(pinfo, -1);

      while (snd_seq_query_next_port(seq, pinfo) >= 0) {
        if ((snd_seq_port_info_get_type(pinfo) &
             SND_SEQ_PORT_TYPE_MIDI_GENERIC)) {
          if ((snd_seq_port_info_get_capability(pinfo) & port_type) ==
              port_type) {
            if (count == port_number) {
              return 1;
            }
            ++count;
          }
        }
      }
    }
  }

  if (port_number == ALL_MIDI_PORTS) {
    return count;
  }
  return 0;
}

static unsigned int _get_port_count(void) {
  snd_seq_port_info_t *pinfo;
  snd_seq_port_info_alloca(&pinfo);
  unsigned int nports = _get_port_info(pinfo, ALL_MIDI_PORTS);
  return nports;
}

static int _open_port(unsigned int port_number) {
  int chip;
  bytes_total = 0;

  unsigned int nports = _get_port_count();
  if (nports < 1) {
    return -1;
  }

  snd_seq_port_info_t *pinfo;
  snd_seq_port_info_alloca(&pinfo);

  if (_get_port_info(pinfo, (int)port_number) == 0) {
    return -1;
  }

  snd_seq_addr_t sender, receiver;
  receiver.client = snd_seq_port_info_get_client(pinfo);
  receiver.port = snd_seq_port_info_get_port(pinfo);
  sender.client = snd_seq_client_id(seq);

  vport = snd_seq_create_simple_port(
      seq, "asid", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
      SND_SEQ_PORT_TYPE_MIDI_GENERIC);

  if (vport < 0) {
    return -1;
  }

  sender.port = vport;
  snd_seq_port_subscribe_malloc(&subscription);
  snd_seq_port_subscribe_set_sender(subscription, &sender);
  snd_seq_port_subscribe_set_dest(subscription, &receiver);
  snd_seq_port_subscribe_set_time_update(subscription, 1);
  snd_seq_port_subscribe_set_time_real(subscription, 1);

  if (snd_seq_subscribe_port(seq, subscription)) {
    return -1;
  }

  queue_id = snd_seq_alloc_queue(seq);
  snd_seq_start_queue(seq, queue_id, NULL);

  if (_send_message(asid_core_start_msg, sizeof(asid_core_start_msg), 0)) {
    log_message(LOG_DEFAULT, "asid start failed");
    return -1;
  }

  for (chip = 0; chip < ASID_CHIPS; ++chip) {
    asid_timing[chip].last_irq = 0;
    asid_timing[chip].start_clock = 0;
  }

  return 0;
}

static char *_get_port_name(unsigned int port_number, char *name_buffer,
                            unsigned int max_name) {
  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  memset(name_buffer, 0, max_name);

  if (_get_port_info(pinfo, (int)port_number)) {
    int cnum = snd_seq_port_info_get_client(pinfo);
    snd_seq_get_any_client_info(seq, cnum, cinfo);
    snprintf(name_buffer, max_name, "%s:%d",
             snd_seq_client_info_get_name(cinfo),
             snd_seq_port_info_get_port(pinfo));
  }

  return name_buffer;
}

static int _send_message(const uint8_t *message, uint8_t message_len,
                         uint64_t nsec) {
  int result;
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, vport);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  result = snd_midi_event_encode(coder, message, message_len, &ev);

  if (result < (int)message_len) {
    log_message(LOG_DEFAULT, "snd_midi_event_encode() failed");
    return -1;
  }
  snd_seq_real_time_t time;
  time.tv_sec = nsec / 1e9;
  time.tv_nsec = nsec - (time.tv_sec * 1e9);
  snd_seq_ev_schedule_real(&ev, queue_id, SND_SEQ_TIME_MODE_REL, &time);
  if (snd_seq_event_output_direct(seq, &ev) < 0) {
    log_message(LOG_DEFAULT, "snd_seq_ev_schedule_real() %lu failed", nsec);
    return -1;
  }
  snd_seq_drain_output(seq);

  bytes_total += message_len;
  return 0;
}

/* Core emit callback: push an encoded SysEx message onto the MIDI queue. */
static void asid_emit(void *ctx, uint8_t chip, const uint8_t *msg, size_t len,
                      uint64_t nsec) {
  (void)ctx;
  (void)chip;
  _send_message(msg, (uint8_t)len, nsec);
}

static int _close_port(void) {
  log_message(LOG_DEFAULT, "%u asid bytes sent, %u bytes saved", bytes_total,
              asid.bytes_saved);
  if (vport != NO_PORT) {
    _send_message(asid_core_stop_msg, sizeof(asid_core_stop_msg), 0);
  }

  snd_seq_remove_events_t *remove_ev;
  snd_seq_remove_events_malloc(&remove_ev);
  snd_seq_remove_events_set_queue(remove_ev, queue_id);
  snd_seq_remove_events_set_condition(remove_ev, SND_SEQ_REMOVE_OUTPUT |
                                                     SND_SEQ_REMOVE_IGNORE_OFF);
  snd_seq_remove_events(seq, remove_ev);
  snd_seq_remove_events_free(remove_ev);
  snd_seq_stop_queue(seq, queue_id, NULL);
  snd_seq_free_queue(seq, queue_id);

  if (vport != NO_PORT) {
    snd_seq_unsubscribe_port(seq, subscription);
    snd_seq_port_subscribe_free(subscription);
    snd_seq_delete_port(seq, vport);
    vport = NO_PORT;
  }
  return 0;
}

static int asid_init(const char *param, int *speed, int *fragsize, int *fragnr,
                     int *channels) {
  int i;
  int nports;
  int asid_param;
  int asid_port;
  int use_update_reg;
  char name_buffer[256];

  *channels = 2;

  if (_initialize_midi()) {
    log_message(LOG_DEFAULT, "failed to initialize MIDI");
    return -1;
  }

  nports = _get_port_count();
  if (nports == 0) {
    log_message(LOG_DEFAULT, "No MIDI ports available");
    return -1;
  }

  log_message(LOG_DEFAULT, "asid open, available ports");
  for (i = 0; i < nports; ++i)
    log_message(LOG_DEFAULT, "Port %d : %s", i,
                _get_port_name(i, name_buffer, sizeof(name_buffer)));

  if (!param) {
    log_message(LOG_DEFAULT, "-soundarg <n> is required");
    return -1;
  }

  asid_param = atoi(param);
  asid_port = asid_param & 1023;
  use_update_reg = (asid_param & 1024) ? 1 : 0;

  if (asid_port < 0 || asid_port > (nports - 1)) {
    log_message(LOG_DEFAULT, "invalid MIDI port in -soundarg");
    return -1;
  }

  if (use_update_reg) {
    log_message(LOG_DEFAULT, "Using asid register update messages");
  }

  asid_core_init(&asid, use_update_reg);

  log_message(LOG_DEFAULT, "Using asid port: %d %s", asid_port,
              _get_port_name(asid_port, name_buffer, sizeof(name_buffer)));
  if (_open_port(asid_port)) {
    log_message(LOG_DEFAULT, "Open port failed");
    return -1;
  }

  return 0;
}

static int asid_dump2(CLOCK clks, CLOCK irq_clks, CLOCK nmi_clks,
                      uint8_t chipno, uint16_t addr, uint8_t byte) {
  CLOCK irq_clk;
  uint8_t reg;

  if (chipno >= ASID_CHIPS) {
    return 0;
  }

  irq_clk = maincpu_int_status->irq_clk;

  /* Flush changes from the previous IRQ.
     TODO: handle sound restarts */
  if (asid_should_flush_on_irq(irq_clk, asid_timing[chipno].last_irq)) {
    uint64_t now = get_clock();
    int64_t n;
    if (asid_timing[chipno].start_clock == 0) {
      asid_timing[chipno].start_clock = now;
    }
    asid_timing[chipno].last_irq = irq_clk;
    n = (int64_t)asid_clock_to_nanos(irq_clk + ASID_CYCLE_PAD) -
        (int64_t)(now - asid_timing[chipno].start_clock);
    if (n < 0) {
      float slip_ms = labs(n) / 1e6;
      if (slip_ms > 1) {
        log_message(LOG_DEFAULT, "asid slip by %fms", slip_ms);
      }
      n = 0;
    }
    asid_core_flush(&asid, chipno, (uint64_t)n, asid_emit, NULL);
  }

  reg = addr & 0x1f;
  if (reg > ASID_MAX_SID_REG) {
    return 0;
  }

  asid_core_set_reg(&asid, chipno, reg, byte, asid_emit, NULL);
  return 0;
}

static int asid_write(int16_t *pbuf, size_t nr) { return 0; }

static void asid_close(void) {
  _close_port();
  snd_midi_event_free(coder);
  snd_seq_close(seq);
}

static int asid_flush(char *state) { return 0; }

static sound_device_t asid_device = {
    "asid",     asid_init, asid_write, NULL, asid_dump2, asid_flush, NULL,
    asid_close, NULL,      NULL,       0,    2,          false};

int sound_init_asid_device(void) { return sound_register_device(&asid_device); }
