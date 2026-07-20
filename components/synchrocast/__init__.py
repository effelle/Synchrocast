"""Authenticated, domain-neutral ESPHome state synchronization."""

import hashlib
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    cover,
    espnow,
    fan,
    sensor,
    text_sensor,
    valve,
)
from esphome.const import CONF_ID
from esphome.core import CORE, HexInt
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
CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_PUBLISH = "publish"
CONF_RECEIVE = "receive"
CONF_SOURCE = "source"
CONF_SYNC_ID = "sync_id"
CONF_MIN_INTERVAL = "min_interval"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_STALE_AFTER = "stale_after"
CONF_DELTA = "delta"
CONF_HEARTBEAT = "heartbeat"
CONF_TRANSPORT = "transport"
CONF_UDP_PORT = "udp_port"
CONF_INTERNAL_ESPNOW_ID = "_espnow_id"
CONF_TRANSPORT_OWNER = "_transport_owner"
CONF_COVER_HANDLER_ID = "_cover_handler_id"
CONF_FAN_HANDLER_ID = "_fan_handler_id"
CONF_VALVE_HANDLER_ID = "_valve_handler_id"
CONF_SENSOR_HANDLER_ID = "_sensor_handler_id"
CONF_BINARY_SENSOR_HANDLER_ID = "_binary_sensor_handler_id"
CONF_TEXT_SENSOR_HANDLER_ID = "_text_sensor_handler_id"

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
SYNCHROCAST_DEFAULT_UDP_PORT = 39581
MAX_ENTITIES_PER_DOMAIN = 16
MAX_COMPONENT_INSTANCES = 8
MAX_TEXT_BYTES = 64
KEY_DERIVATION_PREFIX = b"SYNCHROCAST_V1\x00"
SYNC_ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9_.-]{0,63}")

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
SensorHandler = synchrocast_ns.class_("SensorHandler")
BinarySensorHandler = synchrocast_ns.class_("BinarySensorHandler")
TextSensorHandler = synchrocast_ns.class_("TextSensorHandler")
SynchrocastSensor = synchrocast_ns.class_(
    "SynchrocastSensor", sensor.Sensor
)
SynchrocastBinarySensor = synchrocast_ns.class_(
    "SynchrocastBinarySensor", binary_sensor.BinarySensor
)
SynchrocastTextSensor = synchrocast_ns.class_(
    "SynchrocastTextSensor", text_sensor.TextSensor
)

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


def _has_observable_bindings(item, option):
    block = item.get(option, {})
    return bool(
        isinstance(block, dict)
        and (block.get(CONF_PUBLISH) or block.get(CONF_RECEIVE))
    )


def _is_esp8266_target():
    try:
        return CORE.is_esp8266
    except KeyError:
        return False


_ESPNOW_ID_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_INTERNAL_ESPNOW_ID): cv.use_id(
            espnow.ESPNowComponent
        ),
    },
    extra=cv.ALLOW_EXTRA,
)


def AUTO_LOAD(config):
    domains = {"hmac_sha256"}
    configs = _iter_configs(config)
    if (
        "cfx_sync" not in CORE.loaded_integrations
        and not _is_esp8266_target()
        and any(
            item.get(CONF_TRANSPORT, TRANSPORT_AUTO)
            in (TRANSPORT_AUTO, TRANSPORT_ESPNOW)
            for item in configs
        )
    ):
        domains.add("espnow")
    for item in configs:
        if item.get(CONF_COVERS):
            domains.add("cover")
        if item.get(CONF_FANS):
            domains.add("fan")
        if item.get(CONF_VALVES):
            domains.add("valve")
        if _has_observable_bindings(item, CONF_SENSORS):
            domains.add("sensor")
        if _has_observable_bindings(item, CONF_BINARY_SENSORS):
            domains.add("binary_sensor")
        if _has_observable_bindings(item, CONF_TEXT_SENSORS):
            domains.add("text_sensor")
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


def _validate_sync_id(value):
    value = cv.string_strict(value)
    if not SYNC_ID_PATTERN.fullmatch(value):
        raise cv.Invalid(
            "sync_id must be 1-64 lowercase characters using only "
            "letters, numbers, '.', '_' or '-', and must start with a "
            "letter or number"
        )
    if _fnv1a_32(value) == 0:
        raise cv.Invalid("sync_id hashes to the reserved value 0; rename it")
    return value


def _resolve_transport_owner(config):
    config[CONF_TRANSPORT_OWNER] = (
        OWNER_CFX_SYNC
        if "cfx_sync" in CORE.loaded_integrations
        else OWNER_SYNCHROCAST
    )
    return config


def _validate_transport_dependencies(config):
    if config[CONF_TRANSPORT_OWNER] == OWNER_CFX_SYNC:
        config.pop(CONF_INTERNAL_ESPNOW_ID, None)
        return config

    transport = config[CONF_TRANSPORT]
    if _is_esp8266_target():
        if transport == TRANSPORT_ESPNOW:
            raise cv.Invalid("transport: espnow is available only on ESP32")
        config.pop(CONF_INTERNAL_ESPNOW_ID, None)
        return config

    if transport in (TRANSPORT_AUTO, TRANSPORT_ESPNOW):
        return _ESPNOW_ID_SCHEMA(config)
    config.pop(CONF_INTERNAL_ESPNOW_ID, None)
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


PUBLISH_INTERVAL_SCHEMA = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(
        min=cv.TimePeriod(milliseconds=10),
        max=cv.TimePeriod(seconds=10),
    ),
)
REFRESH_INTERVAL_SCHEMA = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(
        min=cv.TimePeriod(seconds=10),
        max=cv.TimePeriod(hours=1),
    ),
)
STALE_AFTER_SCHEMA = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(
        min=cv.TimePeriod(seconds=10),
        max=cv.TimePeriod(hours=6),
    ),
)


def _publisher_schema(source_type, default_min_interval, *, numeric=False):
    schema = {
        cv.Required(CONF_SOURCE): cv.use_id(source_type),
        cv.Required(CONF_SYNC_ID): _validate_sync_id,
        cv.Optional(
            CONF_MIN_INTERVAL, default=default_min_interval
        ): PUBLISH_INTERVAL_SCHEMA,
        cv.Optional(
            CONF_REFRESH_INTERVAL, default="60s"
        ): REFRESH_INTERVAL_SCHEMA,
    }
    if numeric:
        schema[cv.Optional(CONF_DELTA, default=0.0)] = cv.float_range(min=0)
    return cv.Schema(schema)


SENSOR_PUBLISH_SCHEMA = _publisher_schema(
    sensor.Sensor, "250ms", numeric=True
)
BINARY_SENSOR_PUBLISH_SCHEMA = _publisher_schema(
    binary_sensor.BinarySensor, "50ms"
)
TEXT_SENSOR_PUBLISH_SCHEMA = _publisher_schema(
    text_sensor.TextSensor, "1s"
)

SENSOR_RECEIVE_SCHEMA = sensor.sensor_schema(SynchrocastSensor).extend(
    {
        cv.Required(CONF_SYNC_ID): _validate_sync_id,
        cv.Optional(CONF_STALE_AFTER, default="3min"): STALE_AFTER_SCHEMA,
    }
)
BINARY_SENSOR_RECEIVE_SCHEMA = binary_sensor.binary_sensor_schema(
    SynchrocastBinarySensor
).extend(
    {
        cv.Required(CONF_SYNC_ID): _validate_sync_id,
        cv.Optional(CONF_STALE_AFTER, default="3min"): STALE_AFTER_SCHEMA,
    }
)
TEXT_SENSOR_RECEIVE_SCHEMA = text_sensor.text_sensor_schema(
    SynchrocastTextSensor
).extend(
    {
        cv.Required(CONF_SYNC_ID): _validate_sync_id,
        cv.Optional(CONF_STALE_AFTER, default="3min"): STALE_AFTER_SCHEMA,
    }
)


def _validate_binding_block(block):
    publish = block[CONF_PUBLISH]
    receive = block[CONF_RECEIVE]
    if len(publish) > MAX_ENTITIES_PER_DOMAIN:
        raise cv.Invalid(
            f"at most {MAX_ENTITIES_PER_DOMAIN} publishers are allowed "
            "per observational domain"
        )
    if len(receive) > MAX_ENTITIES_PER_DOMAIN:
        raise cv.Invalid(
            f"at most {MAX_ENTITIES_PER_DOMAIN} receivers are allowed "
            "per observational domain"
        )

    sync_ids = {}
    sources = set()
    for direction, bindings in (
        (CONF_PUBLISH, publish),
        (CONF_RECEIVE, receive),
    ):
        for binding in bindings:
            sync_id = binding[CONF_SYNC_ID]
            sync_hash = _fnv1a_32(sync_id)
            previous = sync_ids.get(sync_hash)
            if previous is not None:
                raise cv.Invalid(
                    f"sync_id '{sync_id}' conflicts with '{previous[1]}' "
                    f"in {previous[0]}"
                )
            sync_ids[sync_hash] = (direction, sync_id)
            if direction == CONF_PUBLISH:
                source_name = _id_name(binding[CONF_SOURCE])
                if source_name in sources:
                    raise cv.Invalid(
                        f"source '{source_name}' is published more than once "
                        "in the same domain"
                    )
                sources.add(source_name)
                if (
                    binding[CONF_REFRESH_INTERVAL].total_milliseconds
                    <= binding[CONF_MIN_INTERVAL].total_milliseconds
                ):
                    raise cv.Invalid(
                        "refresh_interval must be greater than min_interval"
                    )
    return block


def _binding_block_schema(publish_schema, receive_schema):
    return cv.All(
        cv.Schema(
            {
                cv.Optional(CONF_PUBLISH, default=[]): cv.ensure_list(
                    publish_schema
                ),
                cv.Optional(CONF_RECEIVE, default=[]): cv.ensure_list(
                    receive_schema
                ),
            }
        ),
        _validate_binding_block,
    )


SENSOR_BINDING_SCHEMA = _binding_block_schema(
    SENSOR_PUBLISH_SCHEMA, SENSOR_RECEIVE_SCHEMA
)
BINARY_SENSOR_BINDING_SCHEMA = _binding_block_schema(
    BINARY_SENSOR_PUBLISH_SCHEMA, BINARY_SENSOR_RECEIVE_SCHEMA
)
TEXT_SENSOR_BINDING_SCHEMA = _binding_block_schema(
    TEXT_SENSOR_PUBLISH_SCHEMA, TEXT_SENSOR_RECEIVE_SCHEMA
)


def _validate_role_bindings(config):
    has_publishers = any(
        config[option][CONF_PUBLISH]
        for option in (
            CONF_SENSORS,
            CONF_BINARY_SENSORS,
            CONF_TEXT_SENSORS,
        )
    )
    if has_publishers and config[CONF_ROLE] not in (
        ROLE_LEADER,
        ROLE_SATELLITE,
    ):
        raise cv.Invalid(
            "sensor publishers require role: leader or role: satellite; "
            "followers receive state and controllers send commands"
        )
    return config


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
            cv.GenerateID(CONF_SENSOR_HANDLER_ID): cv.declare_id(
                SensorHandler
            ),
            cv.GenerateID(CONF_BINARY_SENSOR_HANDLER_ID): cv.declare_id(
                BinarySensorHandler
            ),
            cv.GenerateID(CONF_TEXT_SENSOR_HANDLER_ID): cv.declare_id(
                TextSensorHandler
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
            cv.Optional(CONF_SENSORS, default={}): SENSOR_BINDING_SCHEMA,
            cv.Optional(
                CONF_BINARY_SENSORS, default={}
            ): BINARY_SENSOR_BINDING_SCHEMA,
            cv.Optional(
                CONF_TEXT_SENSORS, default={}
            ): TEXT_SENSOR_BINDING_SCHEMA,
            cv.Optional(CONF_HEARTBEAT, default="30s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(seconds=10),
                    max=cv.TimePeriod(minutes=5),
                ),
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
    _validate_transport_dependencies,
    _validate_role_bindings,
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


def _validate_global_bindings(all_synchrocast):
    groups = set()
    group_hashes = {}
    standalone_specs = set()
    receiver_ids = set()
    publisher_ids = set()
    for item in all_synchrocast:
        group = item[CONF_GROUP]
        if group in groups:
            raise cv.Invalid(
                f"group '{group}' is configured more than once on this "
                "device; keep all of its domains in one synchrocast block"
            )
        groups.add(group)
        group_hash = _fnv1a_32(group)
        previous_group = group_hashes.get(group_hash)
        if previous_group is not None:
            raise cv.Invalid(
                f"group '{group}' conflicts with '{previous_group}' on the "
                "Synchrocast network; rename one of them"
            )
        group_hashes[group_hash] = group
        if item[CONF_TRANSPORT_OWNER] == OWNER_SYNCHROCAST:
            requested = item[CONF_TRANSPORT]
            uses_udp = requested == TRANSPORT_UDP or (
                requested == TRANSPORT_AUTO and _is_esp8266_target()
            )
            standalone_specs.add(
                (
                    TRANSPORT_UDP if uses_udp else TRANSPORT_ESPNOW,
                    item.get(CONF_UDP_PORT, SYNCHROCAST_DEFAULT_UDP_PORT)
                    if uses_udp
                    else 0,
                )
            )
        for option in (
            CONF_SENSORS,
            CONF_BINARY_SENSORS,
            CONF_TEXT_SENSORS,
        ):
            block = item[option]
            publisher_ids.update(
                _id_name(binding[CONF_SOURCE])
                for binding in block[CONF_PUBLISH]
            )
            receiver_ids.update(
                _id_name(binding[CONF_ID])
                for binding in block[CONF_RECEIVE]
            )
    echoed = publisher_ids & receiver_ids
    if echoed:
        entity_id = sorted(echoed)[0]
        raise cv.Invalid(
            f"Synchrocast receiver '{entity_id}' cannot also be a publisher; "
            "this prevents network echo loops"
        )
    if len(standalone_specs) > 1:
        raise cv.Invalid(
            "all Synchrocast groups on one device must share the same "
            "standalone transport and UDP port"
        )


def _final_validate(config):
    final_config = full_config.get()
    all_synchrocast = _iter_configs(final_config.get("synchrocast", []))
    if len(all_synchrocast) > MAX_COMPONENT_INSTANCES:
        raise cv.Invalid(
            f"at most {MAX_COMPONENT_INSTANCES} synchrocast blocks are "
            "allowed on one device"
        )
    _validate_global_bindings(all_synchrocast)

    sync_configs = final_config.get("cfx_sync", [])
    config[CONF_TRANSPORT_OWNER] = (
        OWNER_CFX_SYNC if _iter_configs(sync_configs) else OWNER_SYNCHROCAST
    )

    if (
        CONF_UDP_PORT in config
        and config[CONF_TRANSPORT] == TRANSPORT_ESPNOW
    ):
        raise cv.Invalid("udp_port cannot be used with transport: espnow")

    if config[CONF_TRANSPORT_OWNER] != OWNER_CFX_SYNC:
        uses_udp = config[CONF_TRANSPORT] == TRANSPORT_UDP or (
            config[CONF_TRANSPORT] == TRANSPORT_AUTO
            and _is_esp8266_target()
        )
        if CONF_UDP_PORT in config and not uses_udp:
            raise cv.Invalid(
                "udp_port requires transport: udp (or auto on ESP8266)"
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


def _derive_key(value):
    return hashlib.sha256(
        KEY_DERIVATION_PREFIX + value.encode("utf-8")
    ).digest()


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


async def _register_sensor_bindings(config, component):
    block = config[CONF_SENSORS]
    if not block[CONF_PUBLISH] and not block[CONF_RECEIVE]:
        return
    handler = cg.new_Pvariable(config[CONF_SENSOR_HANDLER_ID])
    cg.add(handler.set_parent(component))
    for binding in block[CONF_PUBLISH]:
        source = await cg.get_variable(binding[CONF_SOURCE])
        cg.add(
            handler.register_publisher(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                source,
                binding[CONF_MIN_INTERVAL].total_milliseconds,
                binding[CONF_REFRESH_INTERVAL].total_milliseconds,
                binding[CONF_DELTA],
            )
        )
    for binding in block[CONF_RECEIVE]:
        entity = await sensor.new_sensor(binding)
        cg.add(
            handler.register_receiver(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                entity,
                binding[CONF_STALE_AFTER].total_milliseconds,
            )
        )
    cg.add(component.register_domain_handler(handler))


async def _register_binary_sensor_bindings(config, component):
    block = config[CONF_BINARY_SENSORS]
    if not block[CONF_PUBLISH] and not block[CONF_RECEIVE]:
        return
    handler = cg.new_Pvariable(config[CONF_BINARY_SENSOR_HANDLER_ID])
    cg.add(handler.set_parent(component))
    for binding in block[CONF_PUBLISH]:
        source = await cg.get_variable(binding[CONF_SOURCE])
        cg.add(
            handler.register_publisher(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                source,
                binding[CONF_MIN_INTERVAL].total_milliseconds,
                binding[CONF_REFRESH_INTERVAL].total_milliseconds,
            )
        )
    for binding in block[CONF_RECEIVE]:
        entity = await binary_sensor.new_binary_sensor(binding)
        cg.add(
            handler.register_receiver(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                entity,
                binding[CONF_STALE_AFTER].total_milliseconds,
            )
        )
    cg.add(component.register_domain_handler(handler))


async def _register_text_sensor_bindings(config, component):
    block = config[CONF_TEXT_SENSORS]
    if not block[CONF_PUBLISH] and not block[CONF_RECEIVE]:
        return
    handler = cg.new_Pvariable(config[CONF_TEXT_SENSOR_HANDLER_ID])
    cg.add(handler.set_parent(component))
    for binding in block[CONF_PUBLISH]:
        source = await cg.get_variable(binding[CONF_SOURCE])
        cg.add(
            handler.register_publisher(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                source,
                binding[CONF_MIN_INTERVAL].total_milliseconds,
                binding[CONF_REFRESH_INTERVAL].total_milliseconds,
            )
        )
    for binding in block[CONF_RECEIVE]:
        entity = await text_sensor.new_text_sensor(binding)
        cg.add(
            handler.register_receiver(
                _fnv1a_32(binding[CONF_SYNC_ID]),
                entity,
                binding[CONF_STALE_AFTER].total_milliseconds,
            )
        )
    cg.add(component.register_domain_handler(handler))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    owner = config[CONF_TRANSPORT_OWNER]
    if owner == OWNER_CFX_SYNC:
        cg.add_define("USE_SYNCHROCAST_CFX_SYNC_BRIDGE")
    else:
        cg.add_define("USE_SYNCHROCAST_STANDALONE_TRANSPORT")
    if config[CONF_COVERS]:
        cg.add_define("USE_SYNCHROCAST_COVER")
    if config[CONF_FANS]:
        cg.add_define("USE_SYNCHROCAST_FAN")
    if config[CONF_VALVES]:
        cg.add_define("USE_SYNCHROCAST_VALVE")
    if config[CONF_SENSORS][CONF_PUBLISH] or config[CONF_SENSORS][CONF_RECEIVE]:
        cg.add_define("USE_SYNCHROCAST_SENSOR")
    if (
        config[CONF_BINARY_SENSORS][CONF_PUBLISH]
        or config[CONF_BINARY_SENSORS][CONF_RECEIVE]
    ):
        cg.add_define("USE_SYNCHROCAST_BINARY_SENSOR")
    if (
        config[CONF_TEXT_SENSORS][CONF_PUBLISH]
        or config[CONF_TEXT_SENSORS][CONF_RECEIVE]
    ):
        cg.add_define("USE_SYNCHROCAST_TEXT_SENSOR")

    key_bytes = [HexInt(value) for value in _derive_key(config[CONF_KEY])]
    use_standalone_espnow = owner == OWNER_SYNCHROCAST and (
        config[CONF_TRANSPORT] == TRANSPORT_ESPNOW
        or (
            config[CONF_TRANSPORT] == TRANSPORT_AUTO
            and not _is_esp8266_target()
        )
    )
    if use_standalone_espnow:
        espnow_var = await cg.get_variable(config[CONF_INTERNAL_ESPNOW_ID])
        if CORE.using_arduino:
            cg.add_library("WiFi", None)
        cg.add_define("USE_ESPNOW")
        cg.add(espnow_var.set_auto_add_peer(False))
        cg.add(var.set_espnow(espnow_var))

    uses_standalone_udp = owner == OWNER_SYNCHROCAST and (
        config[CONF_TRANSPORT] == TRANSPORT_UDP
        or (
            config[CONF_TRANSPORT] == TRANSPORT_AUTO
            and _is_esp8266_target()
        )
    )
    udp_port = config.get(
        CONF_UDP_PORT,
        SYNCHROCAST_DEFAULT_UDP_PORT if uses_standalone_udp else 0,
    )
    cg.add(var.set_role(ROLE_MAP[config[CONF_ROLE]]))
    cg.add(var.set_group_hash(_fnv1a_32(config[CONF_GROUP])))
    cg.add(var.set_key(key_bytes))
    cg.add(var.set_transport_owner(OWNER_MAP[owner]))
    cg.add(
        var.set_requested_transport(TRANSPORT_MAP[config[CONF_TRANSPORT]])
    )
    cg.add(var.set_requested_udp_port(udp_port))
    cg.add(
        var.set_heartbeat_interval(
            config[CONF_HEARTBEAT].total_milliseconds
        )
    )

    await _register_entities(
        config, var, CONF_COVERS, CONF_COVER_HANDLER_ID
    )
    await _register_entities(config, var, CONF_FANS, CONF_FAN_HANDLER_ID)
    await _register_entities(
        config, var, CONF_VALVES, CONF_VALVE_HANDLER_ID
    )
    await _register_sensor_bindings(config, var)
    await _register_binary_sensor_bindings(config, var)
    await _register_text_sensor_bindings(config, var)
