declare extern type Entity with
end

declare extern type Vec2 with
    x: number
    y: number
    function __add(self, other: Vec2): Vec2
    function __sub(self, other: Vec2): Vec2
    function __mul(self, other: Vec2 | number): Vec2
    function __div(self, other: Vec2 | number): Vec2
    function __unm(self): Vec2
end

declare extern type Vec4 with
    x: number
    y: number
    z: number
    w: number
    function __add(self, other: Vec4): Vec4
    function __sub(self, other: Vec4): Vec4
    function __mul(self, other: Vec4 | number): Vec4
    function __div(self, other: Vec4 | number): Vec4
    function __unm(self): Vec4
end

declare extern type DVec3 with
    x: number
    y: number
    z: number
    function __add(self, other: DVec3): DVec3
    function __sub(self, other: DVec3): DVec3
    function __mul(self, other: DVec3 | number): DVec3
    function __div(self, other: DVec3 | number): DVec3
    function __unm(self): DVec3
    function __len(self): number
    function dot(self, other: DVec3): number
    function cross(self, other: DVec3): DVec3
    function length(self): number
    function length_squared(self): number
    function normalize(self): DVec3
    function distance(self, other: DVec3): number
    function lerp(self, other: DVec3, t: number): DVec3
    function to_vec(self): vector
    function unpack(self): (number, number, number)
end

declare extern type DQuat with
    x: number
    y: number
    z: number
    w: number
    function __mul(self, other: DQuat | DVec3): DQuat | DVec3
    function mul(self, other: DQuat): DQuat
    function rotate(self, v: DVec3): DVec3
    function slerp(self, other: DQuat, t: number): DQuat
    function inverse(self): DQuat
    function conjugate(self): DQuat
    function normalize(self): DQuat
    function dot(self, other: DQuat): number
    function length(self): number
    function to_euler(self): DVec3
    function to_quat(self): Vec4
end

declare extern type ScriptSelf with
    entity: Entity
    props: { [string]: any }
    [string]: any
end

declare scene: {
    exists: (entity: Entity?) -> boolean,
    get: (entity: Entity, component: string, field: string) -> any,
    set: (entity: Entity, component: string, field: string, value: any) -> boolean,
    get64: (entity: Entity, component: string, field: string) -> any,
    set64: (entity: Entity, component: string, field: string, value: DVec3 | DQuat | vector | number) -> boolean,
    get_xyz: (entity: Entity, component: string, field: string) -> (number, number, number),
    set_xyz: (entity: Entity, component: string, field: string, x: number, y: number, z: number) -> boolean,
    find: (name: string) -> Entity?,
    name: (entity: Entity) -> string?,
    query: (component: string) -> { Entity },
    look_at: (from: vector, target: vector) -> vector,
}

declare time: {
    sim_time: () -> number,
    elapsed: () -> number,
    scale: () -> number,
    set_scale: (scale: number) -> (),
    clockrate: (entity: Entity) -> number,
}

declare gpp: {
    ease: (name: string, t: number) -> number,
    random: ((() -> number) & ((max: number) -> number) & ((min: number, max: number) -> number)),
}

declare units: {
    convert: (value: number, from: string, to: string) -> number,
    const: (name: "c" | "G" | "h" | "hbar" | "kB" | "e" | "NA" | "sigma" | "eps0" | "mu0" | "mp" | "me" | "sigmaT" | "Msun" | "Rsun" | "Lsun" | "Mearth" | "Mjup" | "AU" | "pc" | "ly" | "year" | "day") -> number,
}

declare dvec3: {
    new: ((x: number, y: number, z: number) -> DVec3) & (() -> DVec3),
    from_vec: (v: vector | DVec3) -> DVec3,
    to_vec: (v: DVec3) -> vector,
    unpack: (v: DVec3) -> (number, number, number),
    dot: (a: DVec3, b: DVec3) -> number,
    cross: (a: DVec3, b: DVec3) -> DVec3,
    length: (v: DVec3) -> number,
    length_squared: (v: DVec3) -> number,
    normalize: (v: DVec3) -> DVec3,
    distance: (a: DVec3, b: DVec3) -> number,
    lerp: (a: DVec3, b: DVec3, t: number) -> DVec3,
    add: (a: DVec3, b: DVec3) -> DVec3,
    sub: (a: DVec3, b: DVec3) -> DVec3,
}

declare dquat: {
    new: ((x: number, y: number, z: number, w: number) -> DQuat) & (() -> DQuat),
    from_axis_angle: (axis: DVec3 | vector, radians: number) -> DQuat,
    from_euler: ((degrees: DVec3 | vector) -> DQuat) & ((x: number, y: number, z: number) -> DQuat),
    to_euler: (q: DQuat) -> DVec3,
    mul: (a: DQuat, b: DQuat) -> DQuat,
    rotate: (q: DQuat, v: DVec3) -> DVec3,
    slerp: (a: DQuat, b: DQuat, t: number) -> DQuat,
    inverse: (q: DQuat) -> DQuat,
    conjugate: (q: DQuat) -> DQuat,
    normalize: (q: DQuat) -> DQuat,
    dot: (a: DQuat, b: DQuat) -> number,
    length: (q: DQuat) -> number,
    to_quat: (q: DQuat) -> Vec4,
    from_quat: (v: Vec4) -> DQuat,
}

declare ease: (name: string, t: number) -> number
declare vec2: (x: number, y: number) -> Vec2
declare vec4: (x: number, y: number, z: number, w: number) -> Vec4

declare input: {
    axis: (action: string) -> number,
    held: (action: string) -> boolean,
    pressed: (action: string) -> boolean,
    released: (action: string) -> boolean,
    mouse_delta: () -> Vec2,
    scroll: () -> number,
}

declare task: {
    wait: (seconds: number?) -> number,
    frame: () -> number,
    wait_until: (predicate: () -> boolean) -> number,
    wait_event: (name: string) -> ...any,
    spawn: <T...>(fn: (T...) -> (), T...) -> number,
    signal: (name: string, ...any) -> (),
    cancel: (id: number) -> (),
}

declare particles: {
    emitter: (spec: { [string]: any }) -> Entity,
    burst: (emitter: Entity, count: number) -> boolean,
    set_rate: (emitter: Entity, rate: number) -> boolean,
    set_origin: ((emitter: Entity, position: vector | { x: number, y: number, z: number }) -> boolean) & ((emitter: Entity, x: number, y: number, z: number) -> boolean),
    pause: (emitter: Entity) -> boolean,
    resume: (emitter: Entity) -> boolean,
    destroy: (emitter: Entity) -> boolean,
    count: () -> number,
    region_stats: ((center: vector | { x: number, y: number, z: number }, radius: number) -> { count: number, mass: number, mean_temperature: number, mean_velocity: vector }?) & ((x: number, y: number, z: number, radius: number) -> { count: number, mass: number, mean_temperature: number, mean_velocity: vector }?),
}

type HudColor = { r: number, g: number, b: number, a: number? } | { number } | string | number | vector
type HudSpace = "screen" | "left" | "center" | "right"
type HudStyle = { color: HudColor?, thickness: number?, filled: boolean?, space: HudSpace? }
type HudTextStyle = { color: HudColor?, size: number?, align: "left" | "center" | "right"?, space: HudSpace? }
type HudDraw = {
    text: (x: number, y: number, text: any, style: HudTextStyle?) -> (),
    line: (x1: number, y1: number, x2: number, y2: number, style: HudStyle?) -> (),
    rect: (x: number, y: number, w: number, h: number, style: HudStyle?) -> (),
    circle: (x: number, y: number, radius: number, style: HudStyle?) -> (),
    arc: (x: number, y: number, radius: number, from_degrees: number, to_degrees: number, style: HudStyle?) -> (),
    clear: () -> (),
}

declare ui: {
    hud: HudDraw & {
        at: (space: HudSpace) -> HudDraw,
        color: (r: number, g: number, b: number, a: number?) -> HudColor,
    },
}

type Vec3d = { x: number, y: number, z: number }
type PhysicsHit = {
    entity: Entity,
    point: vector,
    normal: vector,
    distance: number,
    point64: DVec3,
}
type JointKind = "fixed" | "hinge" | "distance"
type JointParams = {
    anchor: (vector | Vec3d | DVec3)?,
    axis: (vector | Vec3d | DVec3)?,
    distance: number?,
    min_distance: number?,
    stiffness: number?,
    damping: number?,
    break_force: number?,
    break_torque: number?,
}

declare physics: {
    raycast: (origin: vector | Vec3d | DVec3, direction: vector | Vec3d | DVec3, max_dist: number, layer_mask: number?) -> PhysicsHit?,
    spherecast: (origin: vector | Vec3d | DVec3, radius: number, direction: vector | Vec3d | DVec3, max_dist: number, layer_mask: number?) -> PhysicsHit?,
    overlap: (center: vector | Vec3d | DVec3, radius: number, layer_mask: number?) -> { Entity },
    add_force: (entity: Entity, force: vector | Vec3d | DVec3) -> (),
    add_torque: (entity: Entity, torque: vector | Vec3d | DVec3) -> (),
    add_impulse: (entity: Entity, impulse: vector | Vec3d | DVec3, point: (vector | Vec3d | DVec3)?) -> (),
    set_velocity: (entity: Entity, velocity: vector | Vec3d | DVec3) -> (),
    set_angular_velocity: (entity: Entity, velocity: vector | Vec3d | DVec3) -> (),
    set_kinematic_target: (entity: Entity, position: vector | Vec3d | DVec3, rotation: (vector | Vec3d | DVec3 | DQuat)?) -> (),
    set_gravity_scale: (entity: Entity, scale: number) -> (),
    create_joint: (a: Entity, b: Entity?, kind: JointKind, params: JointParams?) -> number,
    break_joint: (handle: number) -> boolean,
    joint_broken: (handle: number) -> boolean?,
}
