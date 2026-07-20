from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "synchrocast"
PY_COMPONENT = COMPONENT / "__init__.py"
TRANSPORT = COMPONENT / "synchrocast_transport.h"
RUNTIME_HEADER = COMPONENT / "synchrocast_transport_runtime.h"
RUNTIME_SOURCE = COMPONENT / "synchrocast_transport_runtime.cpp"
ADAPTER_HEADER = COMPONENT / "cfx_sync_transport_adapter.h"
ADAPTER_SOURCE = COMPONENT / "cfx_sync_transport_adapter.cpp"
COMPONENT_HEADER = COMPONENT / "synchrocast_component.h"
COMPONENT_SOURCE = COMPONENT / "synchrocast_component.cpp"
AGGREGATE_HEADER = COMPONENT / "synchrocast.h"
TYPES_HEADER = COMPONENT / "synchrocast_types.h"
DISPATCHER_HEADER = COMPONENT / "synchrocast_dispatcher.h"
CODEC_HEADER = COMPONENT / "synchrocast_packet_codec.h"
CODEC_SOURCE = COMPONENT / "synchrocast_packet_codec.cpp"
SENSOR_HEADER = COMPONENT / "sensor_handler.h"
SENSOR_SOURCE = COMPONENT / "sensor_handler.cpp"
BINARY_SENSOR_HEADER = COMPONENT / "binary_sensor_handler.h"
BINARY_SENSOR_SOURCE = COMPONENT / "binary_sensor_handler.cpp"
TEXT_SENSOR_HEADER = COMPONENT / "text_sensor_handler.h"
TEXT_SENSOR_SOURCE = COMPONENT / "text_sensor_handler.cpp"
STANDALONE_HEADER = COMPONENT / "synchrocast_standalone_transport.h"
STANDALONE_SOURCE = COMPONENT / "synchrocast_standalone_transport.cpp"


class SynchrocastArchitectureTests(unittest.TestCase):
    def test_transport_runtime_is_fixed_storage(self):
        text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (TRANSPORT, RUNTIME_HEADER, RUNTIME_SOURCE)
        )

        for forbidden in (
            "std::vector",
            "std::function",
            "std::map",
            "new ",
            "malloc(",
        ):
            self.assertNotIn(forbidden, text)
        self.assertIn("SynchrocastTransportBackend *cfx_sync_backend_", text)
        self.assertIn("SynchrocastTransportPacketSink *sink_", text)

    def test_cfx_owner_never_falls_back_to_standalone(self):
        source = RUNTIME_SOURCE.read_text(encoding="utf-8")

        cfx_branch = re.search(
            r"if \(owner == SynchrocastTransportOwner::CFX_SYNC\) \{"
            r"(.*?)"
            r"\} else if \(owner == "
            r"SynchrocastTransportOwner::SYNCHROCAST\)",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(cfx_branch)
        self.assertIn("cfx_sync_backend_", cfx_branch.group(1))
        self.assertNotIn("standalone_backend_", cfx_branch.group(1))
        self.assertIn(
            "SynchrocastTransportState::WAITING_FOR_CFX_SYNC", source
        )
        self.assertIn("this->block_();", source)

    def test_cfx_adapter_is_explicitly_codegen_gated(self):
        header = ADAPTER_HEADER.read_text(encoding="utf-8")
        source = ADAPTER_SOURCE.read_text(encoding="utf-8")
        python = PY_COMPONENT.read_text(encoding="utf-8")

        define = "USE_SYNCHROCAST_CFX_SYNC_BRIDGE"
        self.assertIn(f"#ifdef {define}", header)
        self.assertIn(f"#ifdef {define}", source)
        self.assertIn(f'cg.add_define("{define}")', python)
        self.assertNotIn("__has_include", header)

    def test_cfx_adapter_checks_bridge_api_and_receive_path(self):
        header = ADAPTER_HEADER.read_text(encoding="utf-8")
        source = ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "CFX_SYNC_SHARED_TRANSPORT_API_VERSION == 1", header
        )
        self.assertIn("CFXSyncReceivePath::UNKNOWN_PEER", source)
        self.assertIn("SynchrocastReceivePath::UNKNOWN_PEER", source)
        self.assertIn("register_shared_transport_consumer(this)", source)
        self.assertIn("unregister_shared_transport_consumer(this)", source)

    def test_cfx_adapter_supports_fixed_raw_send_and_detach(self):
        transport = TRANSPORT.read_text(encoding="utf-8")
        runtime = RUNTIME_SOURCE.read_text(encoding="utf-8")
        header = ADAPTER_HEADER.read_text(encoding="utf-8")
        source = ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("SYNCHROCAST_TRANSPORT_MTU = 250", transport)
        self.assertIn("virtual bool send_broadcast", transport)
        self.assertIn("virtual bool send_to", transport)
        self.assertIn("void detach", header)
        self.assertIn("bus.send_udp(data, size)", source)
        self.assertIn("bus.send_espnow(", source)
        peer_add = source.find("bus.add_espnow_peer(destination.mac.data())")
        unicast_send = source.find(
            "bus.send_espnow(", source.find("bool CFXSyncTransportAdapter::send_to")
        )
        self.assertGreaterEqual(peer_add, 0)
        self.assertLess(peer_add, unicast_send)
        self.assertIn("this->active_backend_->detach(this->sink_)", runtime)

    def test_codegen_detects_configured_cfx_without_dependency(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertIn('"cfx_sync" in CORE.loaded_integrations', python)
        self.assertIn('final_config.get("cfx_sync", [])', python)
        self.assertIn("CONF_TRANSPORT_OWNER", python)
        self.assertNotIn('DEPENDENCIES = ["cfx_sync"]', python)
        self.assertNotRegex(
            python, re.compile(r"AUTO_LOAD\s*=.*cfx_sync", re.DOTALL)
        )
        self.assertNotIn("cv.use_id(CFXSyncComponent)", python)

    def test_codegen_allocates_only_configured_domain_handlers(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")
        aggregate = AGGREGATE_HEADER.read_text(encoding="utf-8")

        self.assertIn("if not entities:\n        return", python)
        for option in (
            "CONF_COVERS",
            "CONF_FANS",
            "CONF_VALVES",
            "CONF_SENSORS",
            "CONF_BINARY_SENSORS",
            "CONF_TEXT_SENSORS",
        ):
            self.assertIn(option, python)
        for handler in (
            "CoverHandler",
            "FanHandler",
            "ValveHandler",
            "SensorHandler",
            "BinarySensorHandler",
            "TextSensorHandler",
        ):
            self.assertIn(handler, python)
        for domain in (
            "COVER",
            "FAN",
            "VALVE",
            "SENSOR",
            "BINARY_SENSOR",
            "TEXT_SENSOR",
        ):
            define = f"USE_SYNCHROCAST_{domain}"
            self.assertIn(f'cg.add_define("{define}")', python)
            self.assertIn(f"#ifdef {define}", aggregate)
            handler = COMPONENT / f"{domain.lower()}_handler.h"
            source = COMPONENT / f"{domain.lower()}_handler.cpp"
            self.assertIn(
                f"#ifdef {define}", handler.read_text(encoding="utf-8")
            )
            self.assertIn(
                f"#ifdef {define}", source.read_text(encoding="utf-8")
            )

    def test_schema_enforces_fixed_registry_limits(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertIn("MAX_ENTITIES_PER_DOMAIN = 16", python)
        self.assertIn("MAX_COMPONENT_INSTANCES = 8", python)
        self.assertIn("MAX_TEXT_BYTES = 64", python)
        self.assertIn("at most {MAX_ENTITIES_PER_DOMAIN} publishers", python)
        self.assertIn("at most {MAX_ENTITIES_PER_DOMAIN} receivers", python)
        self.assertIn("group_hashes", python)
        self.assertIn("conflicts with '{previous_group}'", python)
        self.assertIn("duplicate entity ID", python)
        self.assertIn('"Synchrocast hash; rename one of them"', python)

    def test_observational_schema_is_explicit_and_loop_safe(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")

        for option in (
            "CONF_SOURCE",
            "CONF_SYNC_ID",
            "CONF_MIN_INTERVAL",
            "CONF_REFRESH_INTERVAL",
            "CONF_STALE_AFTER",
            "CONF_DELTA",
        ):
            self.assertIn(option, python)
        self.assertIn("SYNC_ID_PATTERN", python)
        self.assertIn("refresh_interval must be greater than min_interval", python)
        self.assertIn("cannot also be a publisher", python)
        self.assertIn("role: leader or role: satellite", python)
        self.assertIn("sensor.sensor_schema(SynchrocastSensor)", python)
        self.assertIn("binary_sensor.binary_sensor_schema(", python)
        self.assertIn("text_sensor.text_sensor_schema(", python)

    def test_key_is_derived_and_used_by_builtin_codec(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")
        header = COMPONENT_HEADER.read_text(encoding="utf-8")
        source = COMPONENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('KEY_DERIVATION_PREFIX = b"SYNCHROCAST_V1\\x00"', python)
        self.assertIn('domains = {"hmac_sha256"}', python)
        self.assertIn("hashlib.sha256", python)
        self.assertIn("var.set_key", python)
        self.assertIn("std::array<uint8_t, 32> key_", header)
        self.assertIn("SynchrocastPacketCodec::decode", source)
        self.assertIn("SynchrocastPacketCodec::encode", source)

    def test_wire_codec_is_bounded_authenticated_and_explicit(self):
        header = CODEC_HEADER.read_text(encoding="utf-8")
        source = CODEC_SOURCE.read_text(encoding="utf-8")
        combined = header + source

        self.assertIn("HEADER_SIZE = 28", header)
        self.assertIn("AUTH_TAG_SIZE = 16", header)
        self.assertIn("MAX_FRAME_SIZE", header)
        self.assertIn("SYNCHROCAST_TRANSPORT_MTU", header)
        self.assertIn("hmac_sha256::HmacSHA256", source)
        self.assertIn("difference |= left[i] ^ right[i]", source)
        self.assertRegex(
            source,
            re.compile(
                r"size < sizeof\(SYNCHROCAST_MAGIC\).*?NOT_SYNCHROCAST",
                re.DOTALL,
            ),
        )
        self.assertIn("write_u32_", source)
        self.assertIn("read_u32_", source)
        self.assertNotIn("memcpy(output.data(), &packet", source)
        for forbidden in ("std::vector", "std::map", "new ", "malloc("):
            self.assertNotIn(forbidden, combined)

    def test_codec_validates_observational_payloads(self):
        source = CODEC_SOURCE.read_text(encoding="utf-8")

        self.assertIn("std::isfinite(packet.payload.float_val)", source)
        self.assertIn("packet.payload.raw_bytes[0] == 0", source)
        self.assertIn("packet.payload.raw_bytes[0] == 1", source)
        self.assertIn("is_valid_utf8", source)
        self.assertIn("#ifndef USE_SYNCHROCAST_TEXT_SENSOR", source)
        self.assertIn("payload_size > SYNCHROCAST_MAX_PAYLOAD_SIZE", source)

    def test_packet_and_queue_shrink_when_text_is_unused(self):
        types = TYPES_HEADER.read_text(encoding="utf-8")
        dispatcher = DISPATCHER_HEADER.read_text(encoding="utf-8")

        self.assertIn("#ifdef USE_SYNCHROCAST_TEXT_SENSOR", types)
        self.assertIn("sizeof(SynchrocastPacket) == 80", types)
        self.assertIn("sizeof(SynchrocastPacket) == 32", types)
        self.assertIn("QUEUE_CAPACITY = 16", dispatcher)
        self.assertIn("MAX_PACKETS_PER_LOOP = 4", dispatcher)
        self.assertIn(
            "std::array<SynchrocastPacket, QUEUE_CAPACITY> queue_", dispatcher
        )

    def test_observational_handlers_use_fixed_storage(self):
        files = (
            SENSOR_HEADER,
            SENSOR_SOURCE,
            BINARY_SENSOR_HEADER,
            BINARY_SENSOR_SOURCE,
            TEXT_SENSOR_HEADER,
            TEXT_SENSOR_SOURCE,
        )
        combined = "\n".join(path.read_text(encoding="utf-8") for path in files)

        self.assertEqual(combined.count("static constexpr size_t MAX_ENTITIES = 16"), 3)
        self.assertEqual(combined.count("std::array<Publisher, MAX_ENTITIES>"), 3)
        self.assertEqual(combined.count("std::array<Receiver, MAX_ENTITIES>"), 3)
        for forbidden in (
            "add_on_state_callback",
            "std::vector",
            "std::map",
            "new ",
            "malloc(",
        ):
            self.assertNotIn(forbidden, combined)

    def test_receivers_publish_native_state_and_expire(self):
        sensor = SENSOR_SOURCE.read_text(encoding="utf-8")
        binary = BINARY_SENSOR_SOURCE.read_text(encoding="utf-8")
        text_sensor_source = TEXT_SENSOR_SOURCE.read_text(encoding="utf-8")

        self.assertIn("receiver->entity->publish_state(value)", sensor)
        self.assertIn("this->set_has_state(false)", sensor)
        self.assertIn("receiver->entity->publish_state(value)", binary)
        self.assertIn("this->invalidate_state()", BINARY_SENSOR_HEADER.read_text(encoding="utf-8"))
        self.assertIn("receiver->entity->publish_state(", text_sensor_source)
        self.assertIn("this->set_has_state(false)", text_sensor_source)
        for source in (sensor, binary, text_sensor_source):
            self.assertIn("owner_boot_id", source)
            self.assertIn("Ignoring competing publisher", source)
            self.assertIn("expire_receivers_", source)

    def test_text_is_utf8_bounded_and_never_truncated(self):
        header = TEXT_SENSOR_HEADER.read_text(encoding="utf-8")
        source = TEXT_SENSOR_SOURCE.read_text(encoding="utf-8")

        self.assertIn("SYNCHROCAST_MAX_PAYLOAD_SIZE", header)
        self.assertIn("is_valid_utf8", source)
        self.assertIn("no truncation was used", source)
        self.assertNotIn("current_value", header)
        self.assertNotIn("substr(", source)

    def test_attached_udp_port_is_inherited_and_validated(self):
        python = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertIn("CFX_SYNC_UDP_PORT = 39580", python)
        self.assertIn("cv.Optional(CONF_UDP_PORT)", python)
        self.assertNotIn(
            "cv.Optional(CONF_UDP_PORT, default=", python
        )
        self.assertIn(
            "Attached Synchrocast must inherit cfx_sync UDP port", python
        )
        self.assertIn(
            "udp_port cannot be used with transport: espnow", python
        )

    def test_runtime_setup_happens_after_cfx(self):
        header = COMPONENT_HEADER.read_text(encoding="utf-8")

        self.assertIn("setup_priority::LATE - 2.0f", header)

    def test_standalone_transport_is_primary_and_fixed_storage(self):
        header = STANDALONE_HEADER.read_text(encoding="utf-8")
        source = STANDALONE_SOURCE.read_text(encoding="utf-8")
        python = PY_COMPONENT.read_text(encoding="utf-8")
        combined = header + source

        self.assertIn("SYNCHROCAST_DEFAULT_UDP_PORT = 39581", header)
        self.assertIn("MAX_SINKS = 8", header)
        self.assertIn("MAX_UDP_PACKETS_PER_LOOP = 4", header)
        self.assertIn("SYNCHROCAST_TRANSPORT_MTU + 1", source)
        self.assertIn("global_synchrocast_standalone_transport", source)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("register_unknown_peer_handler(this)", source)
        self.assertIn("register_broadcast_handler(this)", source)
        self.assertIn('cg.add_define("USE_SYNCHROCAST_STANDALONE_TRANSPORT")', python)
        self.assertIn('domains.add("espnow")', python)
        self.assertIn("var.set_espnow(espnow_var)", python)
        for forbidden in (
            "std::vector",
            "std::function",
            "std::map",
            "new ",
            "malloc(",
        ):
            self.assertNotIn(forbidden, combined)

    def test_standalone_transport_is_connected_to_runtime(self):
        component = COMPONENT_SOURCE.read_text(encoding="utf-8")
        runtime = RUNTIME_SOURCE.read_text(encoding="utf-8")
        transport = TRANSPORT.read_text(encoding="utf-8")

        self.assertIn("standalone.configure", component)
        self.assertIn("set_standalone_backend(&standalone)", component)
        self.assertIn("this->active_backend_->loop();", runtime)
        self.assertNotIn("STANDALONE_PENDING", transport + runtime)
        self.assertIn('return "standalone active";', runtime)

    def test_transport_frames_use_builtin_codec_replay_and_role_checks(self):
        source = COMPONENT_SOURCE.read_text(encoding="utf-8")
        header = COMPONENT_HEADER.read_text(encoding="utf-8")

        self.assertNotIn("frame_handler_", source + header)
        self.assertIn("SynchrocastPacketCodec::decode", source)
        self.assertIn("SynchrocastPacketCodec::encode", source)
        self.assertIn("role_allows_message_", source)
        self.assertIn("accept_sequence_", source)
        self.assertIn("std::array<ReplayState, 8>", header)
        self.assertIn("Rejected duplicate/stale frame", source)
        self.assertIn("authentication_failures_", source)

if __name__ == "__main__":
    unittest.main()
