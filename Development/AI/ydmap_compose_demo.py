#!/usr/bin/env python3
"""Build a simple item-combine demo map through the local w3x2lni toolchain.

This script works on a copy of a .w3x map:
1. unpack to LNI
2. apply global-variable CRUD edits
3. add minimal item object data
4. add trigger-folder LML files for item synthesis
5. append delayed init code that grants materials to the demo hero
6. repack to a .w3x map
7. unpack again and verify the expected artifacts
"""

from __future__ import annotations

import argparse
import configparser
import os
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
W2L_SCRIPT_DIR = REPO_ROOT / "Development" / "Component" / "plugin" / "w3x2lni" / "script"
LUA_EXE = REPO_ROOT / "Development" / "Component" / "bin" / "lua.exe"
LUA_CPATH = str((REPO_ROOT / "Build" / "publish" / "Debug" / "bin" / "?.dll").resolve()) + ";"

TARGET_HERO = "gg_unit_Hpal_0032"
CUSTOM_ITEM_ID = "I003"
NEW_FOLDER_NAME = "2-物品合成测试"
DEFINED_FORMULA_FILE = "1-DefinedFormula.lml"
COMBINE_EVENT_FILE = "2-合成事件.lml"
GRANT_ITEMS_FILE = "3-发放测试物品.lml"
DEFAULT_KEY = "Def   "

DEFINED_FORMULA_LML = """Event
    TriggerRegisterTimerEventSingle
        Const : 0.00
Condition
Action
    DisplayTextToPlayer
        Preset: Player00
        Const : 0
        Const : 0
        Const : '合成公式：
攻击之爪+6 + 攻击之爪+9 + 合成神符 = 攻击之爪+12
守护指环+2 + 守护指环+3 + 合成神符 = 守护指环+4
6个卡嘉医疗宝石 = 勇气勋章'
    CommentString
        Const : '演示公式1：攻击之爪+6 + 攻击之爪+9 + 合成神符 = 攻击之爪+12'
    YDWENewItemsFormula
        Const : rat6
        Const : 1
        Const : rat9
        Const : 1
        Const : I003
        Const : 1
        Const : gmfr
        Const : 0
        Call  : GetItemTypeId
            Call  : GetLastCreatedItem
        Const : 0
        Const : wolg
        Const : 0
        Const : ratc
    CommentString
        Const : '演示公式2：守护指环+2 + 守护指环+3 + 合成神符 = 守护指环+4'
    YDWENewItemsFormula
        Const : rde1
        Const : 1
        Const : rde2
        Const : 1
        Const : I003
        Const : 1
        Const : gmfr
        Const : 0
        Call  : GetItemTypeId
            Call  : GetLastCreatedItem
        Const : 0
        Const : wolg
        Const : 0
        Const : rde3
    CommentString
        Const : '演示公式3：6个卡嘉医疗宝石 = 勇气勋章'
    YDWENewItemsFormula
        Const : rhth
        Const : 6
        Call  : '        '
            Const : 0
        Const : 0
        Call  : '        '
            Const : 0
        Const : 0
        Call  : '        '
            Const : 0
        Const : 0
        Call  : '        '
            Const : 0
        Const : 0
        Call  : '        '
            Const : 0
        Const : 0
        Const : mcou
    YDWESaveStringByString
        Call  : I2S
            Call  : YDWEConverItemcodeToInt
                Const : ratc
        Const : 合成特效
        Const : Abilities\\Spells\\Items\\AIsm\\AIsmTarget.mdl
    YDWESaveStringByString
        Call  : I2S
            Call  : YDWEConverItemcodeToInt
                Const : rde3
        Const : 合成特效
        Const : Abilities\\Spells\\Items\\AIlm\\AIlmTarget.mdl
    YDWESaveStringByString
        Call  : I2S
            Call  : YDWEConverItemcodeToInt
                Const : mcou
        Const : 合成特效
        Const : Abilities\\Spells\\Other\\Monsoon\\MonsoonBoltTarget.mdl
"""

COMBINE_EVENT_LML = """Event
    YDWESyStemItemCombineRegistTrigger
Condition
Action
    SetVariable
        Var   : compose_count
        Call  : OperatorIntegerAdd
            Var   : compose_count
            Const : 1
    DestroyEffectBJ
        Call  : AddSpecialEffectTarget
            Call  : YDWEGetStringByString
                Call  : I2S
                    Call  : YDWEConverItemcodeToInt
                        Call  : GetItemTypeId
                            Call  : GetLastCombinedItem
                Const : 合成特效
            Call  : GetTriggerUnit
            Const : origin
    DisplayTextToPlayer
        Preset: Player00
        Const : 0
        Const : 0
        Call  : OperatorString
            Call  : OperatorString
                Const : '合成次数: '
                Call  : I2S
                    Var   : compose_count
            Call  : OperatorString
                Const : '  合成了 '
                Call  : GetItemName
                    Call  : GetLastCombinedItem
"""

GRANT_ITEMS_LML = f"""Event
    TriggerRegisterTimerEventSingle
        Const : 0.20
Condition
Action
    SetVariable
        Var   : compose_ready
        Const : 1
    SetVariable
        Var   : compose_count
        Const : 0
    UnitAddItemByIdSwapped
        Const : rat6
        Var   : {TARGET_HERO}
    UnitAddItemByIdSwapped
        Const : rat9
        Var   : {TARGET_HERO}
    UnitAddItemByIdSwapped
        Const : {CUSTOM_ITEM_ID}
        Var   : {TARGET_HERO}
    UnitAddItemByIdSwapped
        Const : rde1
        Var   : {TARGET_HERO}
    UnitAddItemByIdSwapped
        Const : rde2
        Var   : {TARGET_HERO}
    UnitAddItemByIdSwapped
        Const : {CUSTOM_ITEM_ID}
        Var   : {TARGET_HERO}
    CreateItem
        Const : rhth
        Call  : GetUnitX
            Var   : {TARGET_HERO}
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    CreateItem
        Const : rhth
        Call  : OperatorRealAdd
            Call  : GetUnitX
                Var   : {TARGET_HERO}
            Const : 48.00
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    CreateItem
        Const : rhth
        Call  : OperatorRealAdd
            Call  : GetUnitX
                Var   : {TARGET_HERO}
            Const : 96.00
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    CreateItem
        Const : rhth
        Call  : OperatorRealAdd
            Call  : GetUnitX
                Var   : {TARGET_HERO}
            Const : 144.00
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    CreateItem
        Const : rhth
        Call  : OperatorRealAdd
            Call  : GetUnitX
                Var   : {TARGET_HERO}
            Const : 192.00
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    CreateItem
        Const : rhth
        Call  : OperatorRealAdd
            Call  : GetUnitX
                Var   : {TARGET_HERO}
            Const : 240.00
        Call  : OperatorRealAdd
            Call  : GetUnitY
                Var   : {TARGET_HERO}
            Const : 128.00
    DisplayTextToPlayer
        Preset: Player00
        Const : 0
        Const : 0
        Const : '合成测试物品已发放：前两套在背包，6个rhth在脚边'
"""


@dataclass
class GlobalVar:
    name: str
    type_name: str
    options: dict[str, str] = field(default_factory=dict)


def run_w2l(mode: str, input_path: Path, output_path: Path) -> None:
    cmd = [
        str(LUA_EXE),
        "-e",
        "_W2L_MODE='CLI'",
        "main.lua",
        mode,
        str(input_path),
        str(output_path),
    ]
    env = os.environ.copy()
    env["LUA_CPATH"] = LUA_CPATH
    subprocess.run(
        cmd,
        cwd=W2L_SCRIPT_DIR,
        env=env,
        check=True,
        text=True,
    )


def parse_variable_lml(path: Path) -> list[GlobalVar]:
    vars_: list[GlobalVar] = []
    current: GlobalVar | None = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if not line:
            continue
        if not raw_line.startswith("    "):
            if ":" not in line:
                raise ValueError(f"invalid variable line: {raw_line!r}")
            name, type_name = [part.strip() for part in line.split(":", 1)]
            current = GlobalVar(name=name, type_name=type_name)
            vars_.append(current)
            continue
        if current is None:
            raise ValueError(f"orphan variable option: {raw_line!r}")
        key, value = [part.strip() for part in line.strip().split(":", 1)]
        current.options[key] = value
    return vars_


def write_variable_lml(path: Path, vars_: list[GlobalVar]) -> None:
    lines: list[str] = []
    for item in vars_:
        lines.append(f"{item.name}: {item.type_name}")
        for key, value in item.options.items():
            lines.append(f"    {key}: {value}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def upsert_global(vars_: list[GlobalVar], name: str, type_name: str, options: dict[str, str]) -> None:
    for item in vars_:
        if item.name == name:
            item.type_name = type_name
            item.options = dict(options)
            return
    vars_.append(GlobalVar(name=name, type_name=type_name, options=dict(options)))


def delete_global(vars_: list[GlobalVar], name: str) -> bool:
    for idx, item in enumerate(vars_):
        if item.name == name:
            del vars_[idx]
            return True
    return False


def ensure_item_ini(path: Path) -> None:
    if path.exists():
        return
    path.write_text("", encoding="utf-8")


def upsert_custom_item(item_ini: Path) -> None:
    ensure_item_ini(item_ini)
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser.read(item_ini, encoding="utf-8")
    if CUSTOM_ITEM_ID not in parser:
        parser[CUSTOM_ITEM_ID] = {}
    section = parser[CUSTOM_ITEM_ID]
    section["_parent"] = '"tstr"'
    section["Art"] = '"ReplaceableTextures\\\\CommandButtons\\\\BTNBansheeAdept.blp"'
    section["Name"] = '"合成神符"'
    section["abilList"] = '""'
    section["file"] = '"Objects\\\\InventoryItems\\\\runicobject\\\\runicobject.mdl"'
    with item_ini.open("w", encoding="utf-8", newline="\n") as fh:
        parser.write(fh, space_around_delimiters=False)


def ensure_catalog_entry(catalog_path: Path) -> None:
    text = catalog_path.read_text(encoding="utf-8")
    folder_header = f"{NEW_FOLDER_NAME}: 物品合成测试"
    if folder_header in text:
        return
    block = (
        f"{folder_header}\n"
        f"    1-DefinedFormula: DefinedFormula\n"
        f"    2-合成事件: 合成事件\n"
        f"    3-发放测试物品: 发放测试物品\n"
    )
    if not text.endswith("\n"):
        text += "\n"
    text += block
    catalog_path.write_text(text, encoding="utf-8")


def ensure_trigger_files(trigger_root: Path) -> None:
    folder = trigger_root / NEW_FOLDER_NAME
    folder.mkdir(parents=True, exist_ok=True)
    (folder / DEFINED_FORMULA_FILE).write_text(DEFINED_FORMULA_LML, encoding="utf-8")
    (folder / COMBINE_EVENT_FILE).write_text(COMBINE_EVENT_LML, encoding="utf-8")
    (folder / GRANT_ITEMS_FILE).write_text(GRANT_ITEMS_LML, encoding="utf-8")


def apply_global_crud(variable_lml: Path) -> None:
    vars_ = parse_variable_lml(variable_lml)
    # Create
    upsert_global(vars_, "compose_ready", "integer", {DEFAULT_KEY: "0"})
    upsert_global(vars_, "compose_stage", "string", {DEFAULT_KEY: "ready"})
    upsert_global(vars_, "compose_count", "integer", {DEFAULT_KEY: "0"})
    upsert_global(vars_, "compose_temp", "integer", {DEFAULT_KEY: "9"})
    # Modify
    upsert_global(vars_, "compose_stage", "string", {DEFAULT_KEY: "armed"})
    # Delete
    removed = delete_global(vars_, "compose_temp")
    if not removed:
        raise RuntimeError("expected compose_temp to exist before delete")
    write_variable_lml(variable_lml, vars_)


def verify_lni_tree(lni_root: Path) -> None:
    variable_lml = lni_root / "trigger" / "variable.lml"
    vars_ = parse_variable_lml(variable_lml)
    by_name = {item.name: item for item in vars_}
    assert "compose_ready" in by_name, "compose_ready missing"
    ready_default = next((v for k, v in by_name["compose_ready"].options.items() if k.startswith("Def")), None)
    assert ready_default == "0", "compose_ready default mismatch"
    assert "compose_stage" in by_name, "compose_stage missing"
    stage_default = next((v for k, v in by_name["compose_stage"].options.items() if k.startswith("Def")), None)
    assert stage_default == "armed", "compose_stage default mismatch"
    assert "compose_count" in by_name, "compose_count missing"
    count_default = next((v for k, v in by_name["compose_count"].options.items() if k.startswith("Def")), None)
    assert count_default == "0", "compose_count default mismatch"
    assert "compose_temp" not in by_name, "compose_temp should have been deleted"

    item_ini = lni_root / "table" / "item.ini"
    assert item_ini.exists(), "item.ini missing"
    item_text = item_ini.read_text(encoding="utf-8")
    assert f"[{CUSTOM_ITEM_ID}]" in item_text, "custom item section missing"
    assert "合成神符" in item_text, "custom item name missing"

    trigger_folder = lni_root / "trigger" / NEW_FOLDER_NAME
    assert trigger_folder.exists(), "compose trigger folder missing"
    assert (trigger_folder / DEFINED_FORMULA_FILE).exists(), "DefinedFormula trigger missing"
    assert (trigger_folder / COMBINE_EVENT_FILE).exists(), "combine event trigger missing"
    assert (trigger_folder / GRANT_ITEMS_FILE).exists(), "grant items trigger missing"
    grant_text = (trigger_folder / GRANT_ITEMS_FILE).read_text(encoding="utf-8")
    assert "UnitAddItemByIdSwapped" in grant_text, "hero inventory grant missing"
    assert "CreateItem" in grant_text, "ground item grant missing"
    combine_text = (trigger_folder / COMBINE_EVENT_FILE).read_text(encoding="utf-8")
    assert "SetVariable" in combine_text, "compose counter update missing"
    assert "OperatorIntegerAdd" in combine_text, "compose counter increment missing"


def build_demo(input_map: Path, output_map: Path, work_dir: Path) -> dict[str, Path]:
    lni_dir = work_dir / "compose_demo_lni"
    verify_dir = work_dir / "compose_demo_verify_lni"
    if lni_dir.exists():
        shutil.rmtree(lni_dir)
    if verify_dir.exists():
        shutil.rmtree(verify_dir)
    if output_map.exists():
        output_map.unlink()

    run_w2l("lni", input_map, lni_dir)

    trigger_root = lni_dir / "trigger"
    variable_lml = trigger_root / "variable.lml"
    catalog_lml = trigger_root / "catalog.lml"
    item_ini = lni_dir / "table" / "item.ini"

    apply_global_crud(variable_lml)
    ensure_catalog_entry(catalog_lml)
    ensure_trigger_files(trigger_root)
    upsert_custom_item(item_ini)

    verify_lni_tree(lni_dir)
    run_w2l("obj", lni_dir, output_map)
    run_w2l("lni", output_map, verify_dir)
    verify_lni_tree(verify_dir)

    return {
        "lni_dir": lni_dir,
        "verify_dir": verify_dir,
        "output_map": output_map,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Build an item-compose demo map copy")
    parser.add_argument("--input-map", required=True, help="source .w3x map path")
    parser.add_argument("--output-map", required=True, help="output .w3x map path")
    parser.add_argument(
        "--work-dir",
        required=True,
        help="scratch directory used for unpack/verify trees",
    )
    args = parser.parse_args()

    result = build_demo(
        input_map=Path(args.input_map).resolve(),
        output_map=Path(args.output_map).resolve(),
        work_dir=Path(args.work_dir).resolve(),
    )
    print("OK")
    for key, value in result.items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
