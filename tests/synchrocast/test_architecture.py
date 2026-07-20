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
        for option in ("CONF_COVERS", "CONF_FANS", "CONF_VALVES"):
            self.assertIn(option, python)
        for handler in ("CoverHandler", "FanHandler", "ValveHandler"):
            self.assertIn(handler, python)
        for domain in ("COVER", "FAN", "VALVE"):
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
        self.assertIn("duplicate entity ID", python)
        self.assertIn('"Synchrocast hash; rename one of them"', python)

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

    def test_raw_transport_frames_require_a_codec_handler(self):
        source = COMPONENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("if (this->frame_handler_ == nullptr)", source)
        self.assertIn("Shared frame left unclaimed", source)
        self.assertIn("return false;", source)

    def test_missing_standalone_backend_is_reported_as_pending(self):
        transport = TRANSPORT.read_text(encoding="utf-8")
        runtime = RUNTIME_SOURCE.read_text(encoding="utf-8")

        self.assertIn("STANDALONE_PENDING", transport)
        self.assertIn('return "standalone backend pending";', runtime)


if __name__ == "__main__":
    unittest.main()
