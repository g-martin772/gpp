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
    find: (name: string) -> Entity?,
    name: (entity: Entity) -> string?,
    query: (component: string) -> { Entity },
    look_at: (from: vector, target: vector) -> vector,
}

declare gpp: {
    ease: (name: string, t: number) -> number,
    random: ((() -> number) & ((max: number) -> number) & ((min: number, max: number) -> number)),
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
