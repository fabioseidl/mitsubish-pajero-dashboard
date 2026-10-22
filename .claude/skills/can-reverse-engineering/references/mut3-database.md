# MUT-III database findings

What the MUT-III SE service software does and does not tell us about this vehicle.
Source: MUT-III SE update disc `PRE21061_00` (files dated Nov 2021).

## Verdict

The disc carries no measured-value scaling and no broadcast frame definitions.
It is useful only for ECU addressing and bus topology. Do not expect to lift
`pid_map.h` entries from it.

**Do not go looking for a fuller or newer MUT-III copy.** The missing payload is
the CBF/CFF per-ECU descriptions, whose value is DID -> scaling maps for UDS
service 0x22. This vehicle's ECUs answer NRC 0x11 (serviceNotSupported) to Mode
22 for every DID — see "Rejected" in SKILL.md. Scaling for a service the car
refuses is worth nothing. A complete MUT-III install would not change that.

MUT-III reaches this car over K-line with the MUT-II protocol and KWP service
**0x21** (ReadDataByLocalIdentifier), not 0x22. If any service software is ever
worth mining for this truck, the target is its **LID tables for service 0x21** on
the 4M41 — the sniffer's `RDLI` mode speaks exactly that. Mode 22 DID tables are
a dead end here regardless of which disc they come from.

## Where the data lives

Three SQL Server 2008 R2 backups (MTF `TAPE` format), each shipped twice —
`DATABASE/` for the MSDE path, `DATABASE2/` for SQL Server 2014:

| Backup | Size | Content |
|---|---|---|
| `INSTALL/NEWDBIN_V/Database2/CAESARCOMPDB2` | 4.9 MB | CAESAR component DB — **content tables empty** |
| `INSTALL/CANBUS_V/DATABASE2/MUTMCB` | 4.5 MB | CAN bus topology + diagnostic addressing — populated |
| `INSTALL/NEWDBIN0_V/DATABASE2/CFFDB` | 1.8 MB | CFF registry — 1 row, version only |

### CAESARCOMPDB2 is a skeleton

`CC_PRES_MAS`, `ITEM_MAS`, `SCALE_MAS`, `UNIT_MAS`, `RANGE_MAS`, `CBF_MAS`,
`DTC_MAS` and every other content table have **0 rows**. Only catalogs are
populated: `MUTCLASS_EXT` (62035), `VEHICLE_EXT` (11653), `KIND_MAS` (3772),
`SYSTEM_MAS` (94 system names in 25 languages), `TYPE_MAS` (424).

Raw page scans of the backup file do turn up `PRES_*` descriptor strings
carrying units and scaling, e.g.

```
PRES_Pres_hPa_1_hPa_2Byte            2 byte, 1 hPa/bit
PRES_InjMassCyc_0p01_mm_3_cyc_2Byte  2 byte, 0.01 mm^3/cyc
PRES_AirMassPerCyl_0p1x_mg_Hub_2Byte 2 byte, 0.1 mg/stroke
PRES_Prc_0p01220703125x_2Byte        2 byte, 100/8192 %/bit
```

These sit in **unallocated pages** — residue from a populated build, not live
rows. They have no surviving join to any DID or ECU, so they prove only that
such descriptors exist somewhere in Mitsubishi's toolchain. Unusable as-is.

The real per-ECU descriptions are CBF/CFF files that this update disc does not
carry.

## MUTMCB — the usable part

Topology, not signals. Relevant tables:

```
CarInfo(CartypeIxNo, CANBUSID, ConstitutionIxNo, CarTypeInfo)
BusConfigInfo(CANBUSID, BASE_TYPE, COMMENT, UPDATE_DAY)
Constitution(ConstitutionIxNo, CategoryNo, Network, ECUType, JunctionType, ...)
CAN_Category(CategoryNo, CategoryNameIxNo, VerID, KWPComFlg)
CategoryName(CategoryNameIxNo, CategoryName)
KWPCANID(CategoryNameIxNo, SendID, RecvID)
```

`CAN_CategoryMsg.CategoryMsgID` maxes at 213 — message/label indices for the
UI, not CAN IDs.

### This vehicle is `PS41` — not `BK`

The internal project code is **`PS41`**: `PS` = Pajero Sport, `41` = 4M41. The
`CANBUSID` is literal ASCII — `5053343101` spells `PS41`.

A first pass through this database mistakenly matched the car to **`BK`**, which
is the full-size **Pajero IV** (V80/V90). `BK` is the wrong vehicle: it shares
only the 4M41 engine. The Pajero Dakar / Pajero Sport II is Triton-based.

`PS41` configs present:

| CANBUSID | Config | Constitution |
|---|---|---|
| `5053343101` / `02` | PS41 | 4 / 12 |
| `5053343103` / `04` | 06-PS41 (MMAL) MT / AT | 17 / 18 |
| `5053343105` | PS41 EXP, with TCL | 98 |
| `5053343107` | PS41 NAS 10.5MY~ | 168 |
| `5053343108` | PS41 EXP 10.5MY~ | 169 |

Sibling families: `PS24` (2.4), `PS28` (2.8).

ECU set for constitution 169 (`PS41 EXP 10.5MY~`, the closest export match):
`ENG/AT`, `A/C`, `ETACS`, `METER`, `SRS`, `SAS`, `ABS/ASC`, `DISPLAY`.

Note `ENG/AT` is a **combined** engine/transmission category here, and there is
**no `SS4II` category at all** — unlike `BK`, which has one. The transfer case is
not a separate diagnostic node on this platform.

### The negative result that matters

Every `PS41` ECU category has `KWPComFlg = 0` and **no `KWPCANID` row**. Same
conclusion as for `BK`, reached on the right family this time:

Only the engine ECU answers diagnostics over CAN, at the standard OBD-II
`0x7E0`/`0x7E8` the server already uses. ETACS, METER, SRS, SAS and the rest are
K-line (MUT-II protocol) through the VCI, not reachable over CAN. This explains
why bus scans find no diagnostic responder beyond `0x7E8` — it is the vehicle,
not a sniffer limitation. No point spending more `SCAN` time on other addresses.

### Coverage limit

`MUTMCB` contains **no Brazilian entries at all** (no `BRA`/`BRASIL`/`BR`
configs), and the `PS41` rows stop at 2010.5MY. A 2016/2017 Brazilian-assembled
car is not in this database. Everything above is inferred from the same platform
and engine at an earlier model year — treat it as strong background, not as a
statement about this specific car.

## Addresses for other platforms — candidates only, NOT verified here

`KWPCANID` covers the whole Mitsubishi range. These are **later platforms** and
carry no evidence for `BK`. Treat any as an unverified candidate; confirm with
sniffer `SCAN`/`SIG` before trusting one.

| ECU | Request | Response |
|---|---|---|
| ENGINE | `0x7E0` | `0x7E8` |
| AT / CVT / TC-SST | `0x7E1` | `0x7E9` |
| 4WD, SS4, SS4/ES4 | `0x7B6` | `0x7B7` |
| S-AWC, AWC | `0x7B5` | `0x7B4` |
| ABS / ASC / WSS | `0x784` | `0x785` |
| METER | `0x6A0` | `0x514` / `0x51A` |
| ETACS | `0x620` | `0x504` |
| SAS | `0x622` | `0x484` |
| SRS | `0x6E0` | `0x51C` |
| A/C | `0x688` | `0x511` |
| EPS | `0x794` / `0x796` | `0x795` / `0x797` |
| Gateway / SW-BOX | `0x71C` | `0x71D` |
| CAN Conv. | `0x76E` | `0x76D` |
| F.A.S.T. / KOS / WCM | `0x600` | `0x500` |
| AUDIO / MD-6CD | `0x6B0` | `0x516` |
| MMCS / SDA / IDA | `0x7A8` | `0x535` |
| EPB | `0x610` | `0x611` |
| AHB | `0x686` | `0x687` |
| OCM | `0x6E8` | `0x51D` |
| TOPSTACK | `0x720` | `0x721` |
| Leveling / AFS / ACL | `0x77B` | `0x77A` |
| SONAR / Corner | `0x763` | `0x764` |
| AVM / CAMERA | `0x790` | `0x532` |

`0x7B6`/`0x7B7` (SS4/4WD) looked like the best `SCAN` candidate until the
family was corrected: `PS41` has no `SS4II` ECU category at all, so on this
platform the transfer case is not a diagnostic node and the address almost
certainly will not answer. Low priority.

## Reproducing

The backups restore on macOS with no Windows and no MUT-III install:

```bash
docker run -d --platform linux/amd64 --name mut3db \
  -e ACCEPT_EULA=Y -e MSSQL_SA_PASSWORD='<pw>' -e MSSQL_PID=Developer \
  -p 11433:1433 -v /path/to/backups:/bak:ro \
  mcr.microsoft.com/mssql/server:2022-latest
```

```sql
RESTORE DATABASE MUTMCB FROM DISK='/bak/MUTMCB.bak'
 WITH MOVE 'CANBusEditor_Bus_Data' TO '/var/opt/mssql/data/MUTMCB.mdf',
      MOVE 'CANBusEditor_Bus_Log'  TO '/var/opt/mssql/data/MUTMCB.ldf', RECOVERY;
```

Logical names differ per backup — `RESTORE FILELISTONLY` first.
SQL Server 2022 upgrades them from version 661 to 957 on restore.

The disc's `.exe`/`.sys` files are untrusted Windows binaries. Nothing on the
disc needs to be executed to read these databases.
