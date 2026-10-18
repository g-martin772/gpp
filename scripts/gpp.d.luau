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
