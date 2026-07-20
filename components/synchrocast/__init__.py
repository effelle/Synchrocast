"""Domain-neutral ESPHome state synchronization."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover, fan, valve
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome.final_validate import full_config


CODEOWNERS = ["@effelle"]
MULTI_CONF = True
DEPENDENCIES = []

CONF_ROLE = "role"
CONF_GROUP = "group"
CONF_KEY = "key"
CONF_COVERS = "covers"
CONF_FANS = "fans"
CONF_VALVES = "valves"
CONF_HEARTBEAT = "heartbeat"
CONF_TRANSPORT = "transport"
CONF_UDP_PORT = "udp_port"
CONF_TRANSPORT_OWNER = "_transport_owner"
CONF_COVER_HANDLER_ID = "_cover_handler_id"
CONF_FAN_HANDLER_ID = "_fan_handler_id"
CONF_VALVE_HANDLER_ID = "_valve_handler_id"

ROLE_LEADER = "leader"
ROLE_FOLLOWER = "follower"
ROLE_CONTROLLER = "controller"
ROLE_SATELLITE = "satellite"
TRANSPORT_AUTO = "auto"
TRANSPORT_ESPNOW = "espnow"
TRANSPORT_UDP = "udp"
OWNER_SYNCHROCAST = "synchrocast"
OWNER_CFX_SYNC = "cfx_sync"

CFX_SYNC_UDP_PORT = 39580
MAX_ENTITIES_PER_DOMAIN = 16
MAX_COMPONENT_INSTANCES = 8

synchrocast_ns = cg.esphome_ns.namespace("synchrocast")
SynchrocastComponent = synchrocast_ns.class_(
    "SynchrocastComponent", cg.Component
)
SynchrocastRole = synchrocast_ns.enum("SynchrocastRole", is_class=True)
SynchrocastTransportOwner = synchrocast_ns.enum(
    "SynchrocastTransportOwner", is_class=True
)
SynchrocastRequestedTransport = synchrocast_ns.enum(
    "SynchrocastRequestedTransport", is_class=True
)
CoverHandler = synchrocast_ns.class_("CoverHandler")
FanHandler = synchrocast_ns.class_("FanHandler")
ValveHandler = synchrocast_ns.class_("ValveHandler")

ROLE_MAP = {
    ROLE_LEADER: SynchrocastRole.LEADER,
    ROLE_FOLLOWER: SynchrocastRole.FOLLOWER,
    ROLE_CONTROLLER: SynchrocastRole.CONTROLLER,
    ROLE_SATELLITE: SynchrocastRole.SATELLITE,
}
TRANSPORT_MAP = {
    TRANSPORT_AUTO: SynchrocastRequestedTransport.AUTO,
    TRANSPORT_ESPNOW: SynchrocastRequestedTransport.ESPNOW,
    TRANSPORT_UDP: SynchrocastRequestedTransport.UDP,
}
OWNER_MAP = {
    OWNER_SYNCHROCAST: SynchrocastTransportOwner.SYNCHROCAST,
    OWNER_CFX_SYNC: SynchrocastTransportOwner.CFX_SYNC,
}


def _iter_configs(config):
    if isinstance(config, list):
        return [item for item in config if isinstance(item, dict)]
    if isinstance(config, dict):
        return [config]
    return []


def AUTO_LOAD(config):
    domains = set()
    for item in _iter_configs(config):
        if item.get(CONF_COVERS):
            domains.add("cover")
        if item.get(CONF_FANS):
            domains.add("fan")
        if item.get(CONF_VALVES):
            domains.add("valve")
    return sorted(domains)


def _validate_group(value):
    value = cv.string_strict(value).strip()
    if not value:
        raise cv.Invalid("group must not be empty")
    if len(value.encode("utf-8")) > 64:
        raise cv.Invalid("group must be at most 64 UTF-8 bytes")
    return value


def _validate_key(value):
    value = cv.string_strict(value)
    if len(value) < 8:
        raise cv.Invalid("key must contain at least 8 characters")
    if len(value.encode("utf-8")) > 64:
        raise cv.Invalid("key must be at most 64 UTF-8 bytes")
    return value


def _resolve_transport_owner(config):
    config[CONF_TRANSPORT_OWNER] = (
        OWNER_CFX_SYNC
        if "cfx_sync" in CORE.loaded_integrations
        else OWNER_SYNCHROCAST
    )
    return config


def _validate_entity_list(values):
    if len(values) > MAX_ENTITIES_PER_DOMAIN:
        raise cv.Invalid(
            f"at most {MAX_ENTITIES_PER_DOMAIN} entities are allowed "
            "per Synchrocast domain"
        )

    names = set()
    hashes = {}
    for value in values:
        name = _id_name(value)
        if name in names:
            raise cv.Invalid(f"duplicate entity ID '{name}'")
        names.add(name)

        entity_hash = _fnv1a_32(name)
        previous = hashes.get(entity_hash)
        if previous is not None and previous != name:
            raise cv.Invalid(
                f"entity IDs '{previous}' and '{name}' have the same "
                "Synchrocast hash; rename one of them"
            )
        hashes[entity_hash] = name
    return values


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SynchrocastComponent),
            cv.GenerateID(CONF_COVER_HANDLER_ID): cv.declare_id(
                CoverHandler
            ),
            cv.GenerateID(CONF_FAN_HANDLER_ID): cv.declare_id(FanHandler),
            cv.GenerateID(CONF_VALVE_HANDLER_ID): cv.declare_id(
                ValveHandler
            ),
            cv.Required(CONF_ROLE): cv.one_of(
                ROLE_LEADER,
                ROLE_FOLLOWER,
                ROLE_CONTROLLER,
                ROLE_SATELLITE,
                lower=True,
            ),
            cv.Required(CONF_GROUP): _validate_group,
            cv.Required(CONF_KEY): cv.sensitive(_validate_key),
            cv.Optional(CONF_COVERS, default=[]): cv.All(
                cv.ensure_list(cv.use_id(cover.Cover)),
                _validate_entity_list,
            ),
            cv.Optional(CONF_FANS, default=[]): cv.All(
                cv.ensure_list(cv.use_id(fan.Fan)),
                _validate_entity_list,
            ),
            cv.Optional(CONF_VALVES, default=[]): cv.All(
                cv.ensure_list(cv.use_id(valve.Valve)),
                _validate_entity_list,
            ),
            cv.Optional(CONF_HEARTBEAT, default="30s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=10),
                         max=cv.TimePeriod(minutes=5)),
            ),
            cv.Optional(CONF_TRANSPORT, default=TRANSPORT_AUTO): cv.one_of(
                TRANSPORT_AUTO,
                TRANSPORT_ESPNOW,
                TRANSPORT_UDP,
                lower=True,
            ),
            cv.Optional(CONF_UDP_PORT): cv.port,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on(["esp32", "esp8266"]),
    _resolve_transport_owner,
)


def _cfx_transport_set(sync_configs):
    transports = set()
    for item in _iter_configs(sync_configs):
        transport = item.get(CONF_TRANSPORT, TRANSPORT_AUTO)
        role = item.get(CONF_ROLE)
        if transport == TRANSPORT_UDP:
            transports.add(TRANSPORT_UDP)
        elif transport == TRANSPORT_ESPNOW:
            transports.add(TRANSPORT_ESPNOW)
        elif CORE.is_esp8266:
            transports.add(TRANSPORT_UDP)
        else:
            transports.add(TRANSPORT_ESPNOW)
            if role == ROLE_LEADER:
                transports.add(TRANSPORT_UDP)
    return transports


def _final_validate(config):
    final_config = full_config.get()
    all_synchrocast = _iter_configs(final_config.get("synchrocast", []))
    if len(all_synchrocast) > MAX_COMPONENT_INSTANCES:
        raise cv.Invalid(
            f"at most {MAX_COMPONENT_INSTANCES} synchrocast blocks are "
            "allowed on one device"
        )

    sync_configs = final_config.get("cfx_sync", [])
    config[CONF_TRANSPORT_OWNER] = (
        OWNER_CFX_SYNC
        if _iter_configs(sync_configs)
        else OWNER_SYNCHROCAST
    )

    if (
        CONF_UDP_PORT in config
        and config[CONF_TRANSPORT] == TRANSPORT_ESPNOW
    ):
        raise cv.Invalid(
            "udp_port cannot be used with transport: espnow"
        )

    if config[CONF_TRANSPORT_OWNER] != OWNER_CFX_SYNC:
        if CONF_UDP_PORT in config:
            raise cv.Invalid(
                "udp_port is not available until the standalone "
                "Synchrocast transport backend is implemented; remove it"
            )
        return config

    active = _cfx_transport_set(sync_configs)
    requested = config[CONF_TRANSPORT]
    if requested == TRANSPORT_ESPNOW and TRANSPORT_ESPNOW not in active:
        raise cv.Invalid(
            "Synchrocast requests ESP-NOW but the active cfx_sync owner "
            "does not provide ESP-NOW"
        )
    if requested == TRANSPORT_UDP and TRANSPORT_UDP not in active:
        raise cv.Invalid(
            "Synchrocast requests UDP but the active cfx_sync owner does "
            "not provide UDP"
        )
    if CONF_UDP_PORT in config:
        if TRANSPORT_UDP not in active:
            raise cv.Invalid(
                "udp_port cannot be set because the active cfx_sync owner "
                "does not provide UDP"
            )
        if config[CONF_UDP_PORT] != CFX_SYNC_UDP_PORT:
            raise cv.Invalid(
                f"Attached Synchrocast must inherit cfx_sync UDP port "
                f"{CFX_SYNC_UDP_PORT}"
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _fnv1a_32(value):
    result = 0x811C9DC5
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result


def _id_name(value):
    return value.id if hasattr(value, "id") else str(value)


async def _register_entities(config, component, option, handler_id):
    entities = config[option]
    if not entities:
        return
    handler = cg.new_Pvariable(config[handler_id])
    for entity_id in entities:
        entity = await cg.get_variable(entity_id)
        cg.add(
            handler.register_entity(
                _fnv1a_32(_id_name(entity_id)), entity
            )
        )
    cg.add(component.register_domain_handler(handler))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    owner = config[CONF_TRANSPORT_OWNER]
    if owner == OWNER_CFX_SYNC:
        cg.add_define("USE_SYNCHROCAST_CFX_SYNC_BRIDGE")
    if config[CONF_COVERS]:
        cg.add_define("USE_SYNCHROCAST_COVER")
    if config[CONF_FANS]:
        cg.add_define("USE_SYNCHROCAST_FAN")
    if config[CONF_VALVES]:
        cg.add_define("USE_SYNCHROCAST_VALVE")

    cg.add(var.set_role(ROLE_MAP[config[CONF_ROLE]]))
    cg.add(var.set_group_hash(_fnv1a_32(config[CONF_GROUP])))
    cg.add(var.set_transport_owner(OWNER_MAP[owner]))
    cg.add(
        var.set_requested_transport(TRANSPORT_MAP[config[CONF_TRANSPORT]])
    )
    cg.add(var.set_requested_udp_port(config.get(CONF_UDP_PORT, 0)))
    cg.add(
        var.set_heartbeat_interval(
            config[CONF_HEARTBEAT].total_milliseconds
        )
    )

    await _register_entities(
        config,
        var,
        CONF_COVERS,
        CONF_COVER_HANDLER_ID,
    )
    await _register_entities(
        config, var, CONF_FANS, CONF_FAN_HANDLER_ID
    )
    await _register_entities(
        config, var, CONF_VALVES, CONF_VALVE_HANDLER_ID
    )
