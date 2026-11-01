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

declare extern type ScriptSelf with
    entity: Entity
    props: { [string]: any }
    [string]: any
end

declare scene: {
    exists: (entity: Entity?) -> boolean,
    get: (entity: Entity, component: string, field: string) -> any,
    set: (entity: Entity, component: string, field: string, value: any) -> boolean,
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
}

declare gpp: {
    ease: (name: string, t: number) -> number,
    random: ((() -> number) & ((max: number) -> number) & ((min: number, max: number) -> number)),
}

declare units: {
    convert: (value: number, from: string, to: string) -> number,
    const: (name: "c" | "G" | "h" | "hbar" | "kB" | "e" | "NA" | "sigma" | "eps0" | "mu0" | "mp" | "me" | "sigmaT" | "Msun" | "Rsun" | "Lsun" | "Mearth" | "Mjup" | "AU" | "pc" | "ly" | "year" | "day") -> number,
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

type Vec3d = { x: number, y: number, z: number }
type PhysicsHit = {
    entity: Entity,
    point: vector,
    normal: vector,
    distance: number,
    point64: Vec3d,
}
type JointKind = "fixed" | "hinge" | "distance"
type JointParams = {
    anchor: (vector | Vec3d)?,
    axis: (vector | Vec3d)?,
    distance: number?,
    min_distance: number?,
    stiffness: number?,
    damping: number?,
    break_force: number?,
    break_torque: number?,
}

declare physics: {
    raycast: (origin: vector | Vec3d, direction: vector | Vec3d, max_dist: number, layer_mask: number?) -> PhysicsHit?,
    spherecast: (origin: vector | Vec3d, radius: number, direction: vector | Vec3d, max_dist: number, layer_mask: number?) -> PhysicsHit?,
    overlap: (center: vector | Vec3d, radius: number, layer_mask: number?) -> { Entity },
    add_force: (entity: Entity, force: vector | Vec3d) -> (),
    add_torque: (entity: Entity, torque: vector | Vec3d) -> (),
    add_impulse: (entity: Entity, impulse: vector | Vec3d, point: (vector | Vec3d)?) -> (),
    set_velocity: (entity: Entity, velocity: vector | Vec3d) -> (),
    set_angular_velocity: (entity: Entity, velocity: vector | Vec3d) -> (),
    set_kinematic_target: (entity: Entity, position: vector | Vec3d, rotation: (vector | Vec3d)?) -> (),
    set_gravity_scale: (entity: Entity, scale: number) -> (),
    create_joint: (a: Entity, b: Entity?, kind: JointKind, params: JointParams?) -> number,
    break_joint: (handle: number) -> boolean,
    joint_broken: (handle: number) -> boolean?,
}
