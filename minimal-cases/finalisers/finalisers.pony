use "ponybench"
use "random"

primitive PrimitiveInitFinal
  fun _init() =>
    @printf[I32]("primitive init\n".cstring())

  fun _final() =>
    @printf[I32]("primitive final\n".cstring())

class ClassFinal
  fun _final() =>
    @printf[I32]("class final\n".cstring())

actor Main
  let _env: Env
  let bench: PonyBench

  new create(env: Env) =>
    ClassFinal
    _env = env
    bench = PonyBench(_env)
    let mt = MT(4364326)
    var z = mt.next().usize()
    bench_nofinal(z)
    bench_final(z)

  fun bench_nofinal(z: USize) =>
    bench[USize]("nofinal", {()(z = z): USize => let x = TestNoFinal(z); DoNotOptimise[TestNoFinal](x); x.getn() } val)

  fun bench_final(z: USize) =>
    bench[USize]("final", {()(z = z): USize => let x = TestFinal(z); x.getn() } val)

  fun _final() =>
    @printf[I32]("actor final\n".cstring())

class TestNoFinal
  let _n: USize

  new create(n: USize) =>
    _n = n

  fun getn(): USize =>
    _n

class TestFinal
  let _n: USize

  new create(n: USize) =>
    _n = n

  fun getn(): USize =>
    _n

  fun _final() =>
    None
