/* NetHack 5.0 - livesnap.c
 * Native C implementation of Livesnap II serialization and deserialization.
 */

#include "hack.h"
#include "livesnap.h"
#include "yyjson.h"
#include <errno.h>

const char *livesnap_adj_snap = NULL;
const char *livesnap_adj_keys = NULL;
const char *livesnap_adj_out = NULL;

static void
init_livesnap_bases(void)
{
    int i, first, last, prevoclass;
    char oclass;

    for (i = 0; i <= MAXOCLASSES; i++) {
        svb.bases[i] = 0;
    }
    first = MAXOCLASSES;
    prevoclass = -1;
    while (first < NUM_OBJECTS) {
        oclass = objects[first].oc_class;
        last = first + 1;
        while (last < NUM_OBJECTS && objects[last].oc_class == oclass)
            last++;
        svb.bases[(int) oclass] = first;
        first = last;
        prevoclass = (int) oclass;
    }
    svb.bases[MAXOCLASSES] = svb.bases[MAXOCLASSES + 1] = NUM_OBJECTS;
    for (last = MAXOCLASSES - 1; last >= 0; --last)
        if (!svb.bases[last])
            svb.bases[last] = svb.bases[last + 1];

    init_oclass_probs();
}

/* Forward declarations of NetHack C internals if not already in extern.h */

/* Helper to serialize an object recursively */
static yyjson_mut_val *
serialize_c_object(yyjson_mut_doc *doc, struct obj *otmp, boolean on_floor)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "object:%u", (unsigned) otmp->o_id);

    yyjson_mut_obj_add_strcpy(doc, obj, "id", id_buf);
    yyjson_mut_obj_add_uint(doc, obj, "o_id", (uint64_t) otmp->o_id);
    yyjson_mut_obj_add_int(doc, obj, "otyp", otmp->otyp);
    yyjson_mut_obj_add_int(doc, obj, "oclass", otmp->oclass);
    yyjson_mut_obj_add_sint(doc, obj, "quan", (int64_t) otmp->quan);
    yyjson_mut_obj_add_int(doc, obj, "spe", otmp->spe);
    yyjson_mut_obj_add_bool(doc, obj, "blessed", otmp->blessed ? true : false);
    yyjson_mut_obj_add_bool(doc, obj, "cursed", otmp->cursed ? true : false);
    yyjson_mut_obj_add_bool(doc, obj, "unpaid", otmp->unpaid ? true : false);
    yyjson_mut_obj_add_uint(doc, obj, "owornmask", (uint64_t) otmp->owornmask);
    yyjson_mut_obj_add_uint(doc, obj, "owt", (uint64_t) otmp->owt);
    yyjson_mut_obj_add_int(doc, obj, "corpsenm", otmp->corpsenm);
    yyjson_mut_obj_add_int(doc, obj, "invlet", (int) otmp->invlet);
    if (otmp->oeroded) yyjson_mut_obj_add_int(doc, obj, "oeroded", otmp->oeroded);
    if (otmp->oeroded2) yyjson_mut_obj_add_int(doc, obj, "oeroded2", otmp->oeroded2);
    if (otmp->oerodeproof) yyjson_mut_obj_add_bool(doc, obj, "oerodeproof", true);
    if (otmp->rknown) yyjson_mut_obj_add_bool(doc, obj, "rknown", true);
    if (otmp->bknown) yyjson_mut_obj_add_bool(doc, obj, "bknown", true);
    /* always written: a missing dknown restores as 1 (hand-written fixtures) */
    yyjson_mut_obj_add_bool(doc, obj, "dknown", otmp->dknown ? true : false);
    if (otmp->greased) yyjson_mut_obj_add_int(doc, obj, "greased", otmp->greased);
    if (otmp->olocked) yyjson_mut_obj_add_bool(doc, obj, "olocked", true);
    if (otmp->otrapped) yyjson_mut_obj_add_bool(doc, obj, "otrapped", true);
    if (otmp->tknown) yyjson_mut_obj_add_bool(doc, obj, "tknown", true);
    if (otmp->oartifact) yyjson_mut_obj_add_int(doc, obj, "oartifact", otmp->oartifact);

    if (on_floor) {
        yyjson_mut_val *pos = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_int(doc, pos, otmp->ox);
        yyjson_mut_arr_add_int(doc, pos, otmp->oy);
        yyjson_mut_obj_add_val(doc, obj, "position", pos);
    }

    if (Has_contents(otmp)) {
        yyjson_mut_val *contents = yyjson_mut_arr(doc);
        struct obj *c;
        for (c = otmp->cobj; c; c = c->nobj) {
            yyjson_mut_arr_add_val(contents, serialize_c_object(doc, c, FALSE));
        }
        yyjson_mut_obj_add_val(doc, obj, "contents", contents);
    }

    return obj;
}

/* Helper to serialize a monster */
static yyjson_mut_val *
serialize_c_monster(yyjson_mut_doc *doc, struct monst *mtmp)
{
    yyjson_mut_val *mon = yyjson_mut_obj(doc);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "mon:%u", (unsigned) mtmp->m_id);

    yyjson_mut_obj_add_str(doc, mon, "id", id_buf);
    yyjson_mut_obj_add_uint(doc, mon, "m_id", (uint64_t) mtmp->m_id);
    yyjson_mut_obj_add_int(doc, mon, "mnum", monsndx(mtmp->data));

    yyjson_mut_val *pos = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_int(doc, pos, mtmp->mx);
    yyjson_mut_arr_add_int(doc, pos, mtmp->my);
    yyjson_mut_obj_add_val(doc, mon, "position", pos);

    yyjson_mut_val *health = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, health, "current", mtmp->mhp);
    yyjson_mut_obj_add_int(doc, health, "maximum", mtmp->mhpmax);
    yyjson_mut_obj_add_val(doc, mon, "health", health);

    yyjson_mut_obj_add_int(doc, mon, "movement", mtmp->movement);
    yyjson_mut_obj_add_bool(doc, mon, "female", mtmp->female ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "peaceful", mtmp->mpeaceful ? true : false);
    yyjson_mut_obj_add_uint(doc, mon, "tame", (uint64_t) mtmp->mtame);
    yyjson_mut_obj_add_bool(doc, mon, "sleeping", mtmp->msleeping ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "canmove", mtmp->mcanmove ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "cansee", mtmp->mcansee ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "confused", mtmp->mconf ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "fleeing", mtmp->mflee ? true : false);
    yyjson_mut_obj_add_bool(doc, mon, "invisible", mtmp->minvis ? true : false);
    yyjson_mut_obj_add_int(doc, mon, "cham", mtmp->cham);
    if (mtmp->mtrapped) yyjson_mut_obj_add_bool(doc, mon, "trapped", true);

    if (mtmp->minvent) {
        yyjson_mut_val *inv = yyjson_mut_arr(doc);
        struct obj *mo;
        for (mo = mtmp->minvent; mo; mo = mo->nobj) {
            yyjson_mut_arr_add_val(inv, serialize_c_object(doc, mo, FALSE));
        }
        yyjson_mut_obj_add_val(doc, mon, "inventory", inv);
    }

    return mon;
}

/* Serializes the current live game state into a Livesnap II JSON string */
char *
serialize_livesnap_bundle(void)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return NULL;

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "format", "nethack-livesnap-bundle-1");
    yyjson_mut_obj_add_int(doc, root, "schemaVersion", 1);

    /* --- Manifest --- */
    yyjson_mut_val *manifest = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, manifest, "format", "nethack-livesnap-manifest-1");
    yyjson_mut_obj_add_int(doc, manifest, "schemaVersion", 1);
    yyjson_mut_obj_add_str(doc, manifest, "profile", "continuous");

    yyjson_mut_val *ruleset = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, ruleset, "name", "NetHack");
    yyjson_mut_obj_add_str(doc, ruleset, "version", "5.0.0");
    yyjson_mut_obj_add_str(doc, ruleset, "variant", "upstream");
    yyjson_mut_obj_add_val(doc, manifest, "ruleset", ruleset);

    yyjson_mut_obj_add_str(doc, manifest, "gameId", "game:nethack-c");
    yyjson_mut_obj_add_str(doc, manifest, "generation", "1");

    char lvl_name[32];
    snprintf(lvl_name, sizeof(lvl_name), "level:%d", (int) u.uz.dlevel);
    yyjson_mut_obj_add_strcpy(doc, manifest, "currentLevel", lvl_name);

    yyjson_mut_val *chk = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, chk, "kind", "top-level-command-boundary");
    char moves_buf[32];
    snprintf(moves_buf, sizeof(moves_buf), "%ld", (long) svm.moves);
    yyjson_mut_obj_add_strcpy(doc, chk, "moves", moves_buf);
    yyjson_mut_obj_add_str(doc, chk, "inputSequence", "0");
    yyjson_mut_obj_add_strcpy(doc, chk, "turnSequence", moves_buf);
    yyjson_mut_obj_add_val(doc, manifest, "checkpoint", chk);

    yyjson_mut_val *glob_ref = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, glob_ref, "partId", "global");
    yyjson_mut_obj_add_str(doc, glob_ref, "format", "nethack-livesnap-global-1");
    yyjson_mut_obj_add_val(doc, manifest, "global", glob_ref);

    yyjson_mut_val *levels_arr = yyjson_mut_arr(doc);
    yyjson_mut_val *lvl_ref = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, lvl_ref, "levelId", lvl_name);
    yyjson_mut_val *lvl_part = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, lvl_part, "partId", lvl_name);
    yyjson_mut_obj_add_str(doc, lvl_part, "format", "nethack-livesnap-level-1");
    yyjson_mut_obj_add_val(doc, lvl_ref, "part", lvl_part);
    yyjson_mut_val *room_order = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, lvl_ref, "roomOrder", room_order);
    yyjson_mut_arr_add_val(levels_arr, lvl_ref);
    yyjson_mut_obj_add_val(doc, manifest, "levels", levels_arr);

    yyjson_mut_val *reqs = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, manifest, "requirements", reqs);

    yyjson_mut_val *exts = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, manifest, "extensions", exts);

    yyjson_mut_val *prod = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, prod, "port", "nethack-c");
    yyjson_mut_obj_add_str(doc, prod, "version", "5.0.0");
    yyjson_mut_obj_add_val(doc, manifest, "producer", prod);

    yyjson_mut_obj_add_val(doc, root, "manifest", manifest);

    /* --- Parts --- */
    yyjson_mut_val *parts = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "parts", parts);

    /* --- Global Part --- */
    yyjson_mut_val *global = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, global, "format", "nethack-livesnap-global-1");
    yyjson_mut_obj_add_int(doc, global, "schemaVersion", 1);
    yyjson_mut_obj_add_str(doc, global, "partId", "global");

    yyjson_mut_val *gtime = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, gtime, "moves", moves_buf);
    yyjson_mut_obj_add_str(doc, gtime, "turnSequence", "0");
    yyjson_mut_obj_add_val(doc, global, "time", gtime);

    /* Hero state */
    yyjson_mut_val *hero = yyjson_mut_obj(doc);
    yyjson_mut_val *hero_pos = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_int(doc, hero_pos, u.ux);
    yyjson_mut_arr_add_int(doc, hero_pos, u.uy);
    yyjson_mut_obj_add_val(doc, hero, "position", hero_pos);

    yyjson_mut_val *hero_hp = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, hero_hp, "current", u.uhp);
    yyjson_mut_obj_add_int(doc, hero_hp, "maximum", u.uhpmax);
    yyjson_mut_obj_add_val(doc, hero, "health", hero_hp);

    yyjson_mut_val *hero_en = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, hero_en, "current", u.uen);
    yyjson_mut_obj_add_int(doc, hero_en, "maximum", u.uenmax);
    yyjson_mut_obj_add_val(doc, hero, "energy", hero_en);

    yyjson_mut_val *hero_exp = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, hero_exp, "currentLevel", u.ulevel);
    yyjson_mut_obj_add_val(doc, hero, "experience", hero_exp);

    yyjson_mut_val *hero_attrs = yyjson_mut_obj(doc);
    static const char *attr_names[] = { "strength", "intelligence", "wisdom", "dexterity", "constitution", "charisma" };
    int a;
    for (a = 0; a < 6; a++) {
        yyjson_mut_val *att = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, att, "current", ABASE(a));
        yyjson_mut_obj_add_int(doc, att, "maximum", AMAX(a));
        yyjson_mut_obj_add_val(doc, hero_attrs, attr_names[a], att);
    }
    yyjson_mut_obj_add_val(doc, hero, "attributes", hero_attrs);

    yyjson_mut_val *hero_luck = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, hero_luck, "base", u.uluck);
    yyjson_mut_obj_add_int(doc, hero_luck, "carriedBonus", u.moreluck);
    yyjson_mut_obj_add_val(doc, hero, "luck", hero_luck);

    yyjson_mut_val *hero_hunger = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, hero_hunger, "nutrition", u.uhunger);
    yyjson_mut_obj_add_int(doc, hero_hunger, "state", u.uhs);
    yyjson_mut_obj_add_val(doc, hero, "hunger", hero_hunger);
    yyjson_mut_obj_add_int(doc, hero, "lycanthropy", u.ulycn);

    if (u.utrap) {
        yyjson_mut_val *trap_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, trap_obj, "time", (uint64_t) u.utrap);
        yyjson_mut_obj_add_uint(doc, trap_obj, "type", (uint64_t) u.utraptype);
        yyjson_mut_obj_add_val(doc, hero, "trapped", trap_obj);
    }

    if (u.uinwater) {
        yyjson_mut_obj_add_bool(doc, hero, "uinwater", true);
    }

    if (u.uundetected) {
        yyjson_mut_obj_add_bool(doc, hero, "uundetected", true);
    }

    if (Upolyd) {
        yyjson_mut_val *polyd_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, polyd_obj, "monnum", u.umonnum);
        yyjson_mut_obj_add_int(doc, polyd_obj, "mh", u.mh);
        yyjson_mut_obj_add_int(doc, polyd_obj, "mhmax", u.mhmax);
        yyjson_mut_obj_add_int(doc, polyd_obj, "mtimedone", u.mtimedone);
        yyjson_mut_obj_add_val(doc, hero, "polyd", polyd_obj);
    }
    if (u.usteed)
        yyjson_mut_obj_add_uint(doc, hero, "steed_mid", u.usteed->m_id);

    if (HHallucination)
        yyjson_mut_obj_add_int(doc, hero, "hallucinating", (int) (HHallucination & TIMEOUT));
    if (Slimed)
        yyjson_mut_obj_add_int(doc, hero, "slimed", (int) (Slimed & TIMEOUT));

    /* Hero inventory */
    yyjson_mut_val *hero_inv = yyjson_mut_arr(doc);
    struct obj *otmp;
    for (otmp = gi.invent; otmp; otmp = otmp->nobj) {
        yyjson_mut_arr_add_val(hero_inv, serialize_c_object(doc, otmp, FALSE));
    }
    yyjson_mut_obj_add_val(doc, hero, "inventory", hero_inv);

    yyjson_mut_obj_add_val(doc, global, "hero", hero);
    yyjson_mut_obj_add_val(doc, parts, "global", global);

    /* --- Level Part --- */
    yyjson_mut_val *lvl = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, lvl, "format", "nethack-livesnap-level-1");
    yyjson_mut_obj_add_int(doc, lvl, "schemaVersion", 1);
    yyjson_mut_obj_add_strcpy(doc, lvl, "partId", lvl_name);
    yyjson_mut_obj_add_strcpy(doc, lvl, "levelId", lvl_name);

    yyjson_mut_val *lvl_id = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, lvl_id, "dnum", u.uz.dnum);
    yyjson_mut_obj_add_int(doc, lvl_id, "dlevel", u.uz.dlevel);
    yyjson_mut_obj_add_val(doc, lvl, "identity", lvl_id);

    yyjson_mut_val *lvl_flags = yyjson_mut_obj(doc);
    if (svl.level.flags.sokoban_rules)
        yyjson_mut_obj_add_bool(doc, lvl_flags, "sokoban_rules", true);
    if (svl.level.flags.hardfloor)
        yyjson_mut_obj_add_bool(doc, lvl_flags, "hardfloor", true);
    yyjson_mut_obj_add_val(doc, lvl, "flags", lvl_flags);

    /* Grid Map */
    yyjson_mut_val *map_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *dim = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_int(doc, dim, COLNO);
    yyjson_mut_arr_add_int(doc, dim, ROWNO);
    yyjson_mut_obj_add_val(doc, map_obj, "dimensions", dim);

    yyjson_mut_val *locations = yyjson_mut_arr(doc);
    int x, y;
    for (x = 0; x < COLNO; x++) {
        yyjson_mut_val *col = yyjson_mut_arr(doc);
        for (y = 0; y < ROWNO; y++) {
            yyjson_mut_val *cell = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, cell, "typ", levl[x][y].typ);
            yyjson_mut_obj_add_int(doc, cell, "lit", levl[x][y].lit);
            yyjson_mut_obj_add_int(doc, cell, "seen", levl[x][y].seenv);
            yyjson_mut_obj_add_int(doc, cell, "waslit", levl[x][y].waslit);
            yyjson_mut_obj_add_int(doc, cell, "flags", levl[x][y].flags);
            yyjson_mut_obj_add_int(doc, cell, "horizontal", levl[x][y].horizontal);
            yyjson_mut_arr_add_val(col, cell);
        }
        yyjson_mut_arr_add_val(locations, col);
    }
    yyjson_mut_obj_add_val(doc, map_obj, "locations", locations);
    yyjson_mut_obj_add_val(doc, lvl, "map", map_obj);

    /* Entities */
    yyjson_mut_val *entities = yyjson_mut_obj(doc);

    /* Monsters */
    yyjson_mut_val *mons_arr = yyjson_mut_arr(doc);
    struct monst *mtmp;
    for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
        yyjson_mut_arr_add_val(mons_arr, serialize_c_monster(doc, mtmp));
    }
    yyjson_mut_obj_add_val(doc, entities, "monsters", mons_arr);

    /* Floor Objects */
    yyjson_mut_val *objs_arr = yyjson_mut_arr(doc);
    for (otmp = fobj; otmp; otmp = otmp->nobj) {
        yyjson_mut_arr_add_val(objs_arr, serialize_c_object(doc, otmp, TRUE));
    }
    yyjson_mut_obj_add_val(doc, entities, "objects", objs_arr);

    /* Traps */
    yyjson_mut_val *traps_arr = yyjson_mut_arr(doc);
    struct trap *ttmp;
    for (ttmp = gf.ftrap; ttmp; ttmp = ttmp->ntrap) {
        yyjson_mut_val *tr = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, tr, "tx", ttmp->tx);
        yyjson_mut_obj_add_int(doc, tr, "ty", ttmp->ty);
        yyjson_mut_obj_add_int(doc, tr, "ttyp", ttmp->ttyp);
        yyjson_mut_obj_add_int(doc, tr, "tseen", ttmp->tseen);
        if (ttmp->once) yyjson_mut_obj_add_bool(doc, tr, "once", true);
        if (ttmp->madeby_u) yyjson_mut_obj_add_bool(doc, tr, "madeby_u", true);
        if (ttmp->launch.x || ttmp->launch.y) {
            yyjson_mut_val *l = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, l, "x", ttmp->launch.x);
            yyjson_mut_obj_add_int(doc, l, "y", ttmp->launch.y);
            yyjson_mut_obj_add_val(doc, tr, "launch", l);
        }
        if (ttmp->launch2.x || ttmp->launch2.y) {
            yyjson_mut_val *l2 = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, l2, "x", ttmp->launch2.x);
            yyjson_mut_obj_add_int(doc, l2, "y", ttmp->launch2.y);
            yyjson_mut_obj_add_val(doc, tr, "launch2", l2);
        }
        yyjson_mut_arr_add_val(traps_arr, tr);
    }
    yyjson_mut_obj_add_val(doc, entities, "traps", traps_arr);

    /* Engravings */
    yyjson_mut_val *engr_arr = yyjson_mut_arr(doc);
    struct engr *ep;
    for (ep = head_engr; ep; ep = ep->nxt_engr) {
        yyjson_mut_val *e = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, e, "tx", ep->engr_x);
        yyjson_mut_obj_add_int(doc, e, "ty", ep->engr_y);
        yyjson_mut_obj_add_int(doc, e, "etype", ep->engr_type);
        const char *txt = ep->engr_txt[actual_text];
        yyjson_mut_obj_add_strcpy(doc, e, "text", txt ? txt : "");
        yyjson_mut_arr_add_val(engr_arr, e);
    }
    yyjson_mut_obj_add_val(doc, entities, "engravings", engr_arr);

    /* Stairs */
    yyjson_mut_val *stairs_arr = yyjson_mut_arr(doc);
    stairway *stway;
    for (stway = gs.stairs; stway; stway = stway->next) {
        yyjson_mut_val *st = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, st, "sx", stway->sx);
        yyjson_mut_obj_add_int(doc, st, "sy", stway->sy);
        yyjson_mut_obj_add_bool(doc, st, "up", stway->up ? true : false);
        yyjson_mut_obj_add_bool(doc, st, "isladder", stway->isladder ? true : false);
        yyjson_mut_arr_add_val(stairs_arr, st);
    }
    yyjson_mut_obj_add_val(doc, entities, "stairs", stairs_arr);

    yyjson_mut_obj_add_val(doc, lvl, "entities", entities);
    yyjson_mut_val *lvl_key = yyjson_mut_strcpy(doc, lvl_name);
    yyjson_mut_obj_add(parts, lvl_key, lvl);

    /* Write JSON string */
    yyjson_write_err werr;
    char *json = yyjson_mut_write_opts(doc, YYJSON_WRITE_PRETTY | YYJSON_WRITE_ALLOW_INVALID_UNICODE | YYJSON_WRITE_ESCAPE_UNICODE, NULL, NULL, &werr);
    if (!json) {
        fprintf(stderr, "[LIVESNAP] yyjson_mut_write failed: code=%u msg=%s\n", werr.code, werr.msg);
    }
    yyjson_mut_doc_free(doc);
    return json;
}

/* Saves the current live game state into a Livesnap II JSON file */
int
save_livesnap_to_file(const char *path)
{
    char *json = serialize_livesnap_bundle();
    if (!json) {
        fprintf(stderr, "[LIVESNAP] serialize_livesnap_bundle returned NULL!\n");
        return 0;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[LIVESNAP] fopen('%s') failed: %s\n", path, strerror(errno));
        free(json);
        return 0;
    }
    fputs(json, fp);
    fclose(fp);
    free(json);
    return 1;
}

/* Helper to parse and instantiate an object */
static struct obj *
deserialize_c_object(yyjson_val *obj_val, boolean on_floor)
{
    if (!obj_val) return NULL;
    struct obj *o = (struct obj *) alloc(sizeof(struct obj));
    memset((genericptr_t) o, 0, sizeof(struct obj));

    o->o_id = (unsigned) yyjson_get_uint(yyjson_obj_get(obj_val, "o_id"));
    o->otyp = (short) yyjson_get_int(yyjson_obj_get(obj_val, "otyp"));
    yyjson_val *oclass_v = yyjson_obj_get(obj_val, "oclass");
    if (oclass_v) {
        o->oclass = (schar) yyjson_get_int(oclass_v);
    } else if (o->otyp >= 0 && o->otyp < NUM_OBJECTS) {
        o->oclass = objects[o->otyp].oc_class;
    }
    o->quan = (long) yyjson_get_sint(yyjson_obj_get(obj_val, "quan"));
    if (o->quan <= 0) o->quan = 1;
    o->spe = (schar) yyjson_get_int(yyjson_obj_get(obj_val, "spe"));
    o->blessed = yyjson_get_bool(yyjson_obj_get(obj_val, "blessed")) ? 1 : 0;
    o->cursed = yyjson_get_bool(yyjson_obj_get(obj_val, "cursed")) ? 1 : 0;
    o->unpaid = yyjson_get_bool(yyjson_obj_get(obj_val, "unpaid")) ? 1 : 0;
    o->owornmask = (long) yyjson_get_uint(yyjson_obj_get(obj_val, "owornmask"));
    yyjson_val *cnm_v = yyjson_obj_get(obj_val, "corpsenm");
    if (cnm_v) {
        o->corpsenm = (short) yyjson_get_int(cnm_v);
    } else {
        o->corpsenm = NON_PM;
    }
    yyjson_val *invlet_v = yyjson_obj_get(obj_val, "invlet");
    if (invlet_v) {
        if (yyjson_is_str(invlet_v)) {
            o->invlet = yyjson_get_str(invlet_v)[0];
        } else {
            o->invlet = (char) yyjson_get_int(invlet_v);
        }
    }
    yyjson_val *owt_v = yyjson_obj_get(obj_val, "owt");
    if (owt_v) {
        o->owt = (unsigned) yyjson_get_uint(owt_v);
    } else {
        o->owt = (unsigned) weight(o);
    }
    yyjson_val *oeroded_v = yyjson_obj_get(obj_val, "oeroded");
    if (oeroded_v) o->oeroded = (uchar) yyjson_get_int(oeroded_v);
    yyjson_val *oeroded2_v = yyjson_obj_get(obj_val, "oeroded2");
    if (oeroded2_v) o->oeroded2 = (uchar) yyjson_get_int(oeroded2_v);
    yyjson_val *oerodeproof_v = yyjson_obj_get(obj_val, "oerodeproof");
    if (oerodeproof_v) o->oerodeproof = yyjson_get_bool(oerodeproof_v) ? 1 : yyjson_get_int(oerodeproof_v) ? 1 : 0;
    yyjson_val *rknown_v = yyjson_obj_get(obj_val, "rknown");
    if (rknown_v) o->rknown = yyjson_get_bool(rknown_v) ? 1 : yyjson_get_int(rknown_v) ? 1 : 0;
    yyjson_val *bknown_v = yyjson_obj_get(obj_val, "bknown");
    if (bknown_v) o->bknown = yyjson_get_bool(bknown_v) ? 1 : yyjson_get_int(bknown_v) ? 1 : 0;
    yyjson_val *dknown_v = yyjson_obj_get(obj_val, "dknown");
    o->dknown = dknown_v ? (yyjson_get_bool(dknown_v) ? 1 : yyjson_get_int(dknown_v) ? 1 : 0) : 1;
    yyjson_val *greased_v = yyjson_obj_get(obj_val, "greased");
    if (greased_v) o->greased = (uchar) (yyjson_get_bool(greased_v) ? 1 : yyjson_get_int(greased_v));
    yyjson_val *olocked_v = yyjson_obj_get(obj_val, "olocked");
    if (olocked_v) o->olocked = yyjson_get_bool(olocked_v) ? 1 : yyjson_get_int(olocked_v) ? 1 : 0;
    yyjson_val *otrapped_v = yyjson_obj_get(obj_val, "otrapped");
    if (otrapped_v) o->otrapped = yyjson_get_bool(otrapped_v) ? 1 : yyjson_get_int(otrapped_v) ? 1 : 0;
    yyjson_val *tknown_v = yyjson_obj_get(obj_val, "tknown");
    if (tknown_v) o->tknown = yyjson_get_bool(tknown_v) ? 1 : yyjson_get_int(tknown_v) ? 1 : 0;
    yyjson_val *oart_v = yyjson_obj_get(obj_val, "oartifact");
    if (oart_v) o->oartifact = (uchar) yyjson_get_int(oart_v);

    if (on_floor) {
        yyjson_val *pos = yyjson_obj_get(obj_val, "position");
        if (pos) {
            o->ox = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 0));
            o->oy = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 1));
        }
        o->where = OBJ_FREE;
    } else {
        o->where = OBJ_INVENT;
    }

    /* Container contents */
    yyjson_val *contents = yyjson_obj_get(obj_val, "contents");
    if (contents && yyjson_is_arr(contents)) {
        size_t idx, max;
        yyjson_val *cval;
        struct obj *prev_c = NULL;
        yyjson_arr_foreach(contents, idx, max, cval) {
            struct obj *c = deserialize_c_object(cval, FALSE);
            if (c) {
                c->where = OBJ_CONTAINED;
                c->ocontainer = o;
                if (!prev_c) {
                    o->cobj = c;
                } else {
                    prev_c->nobj = c;
                }
                prev_c = c;
            }
        }
    }

    return o;
}

/* Helper to parse and instantiate a monster */
static struct monst *
deserialize_c_monster(yyjson_val *mon_val)
{
    if (!mon_val) return NULL;
    struct monst *mtmp = newmonst();
    if (!mtmp) return NULL;
    memset((genericptr_t) mtmp, 0, sizeof(struct monst));

    mtmp->m_id = (unsigned) yyjson_get_uint(yyjson_obj_get(mon_val, "m_id"));
    int mnum = yyjson_get_int(yyjson_obj_get(mon_val, "mnum"));
    if (mnum >= 0 && mnum < NUMMONS) {
        mtmp->data = &mons[mnum];
    } else {
        mtmp->data = &mons[PM_KITTEN];
    }

    yyjson_val *pos = yyjson_obj_get(mon_val, "position");
    if (pos) {
        mtmp->mx = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 0));
        mtmp->my = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 1));
    }

    yyjson_val *health = yyjson_obj_get(mon_val, "health");
    if (health) {
        mtmp->mhp = (short) yyjson_get_int(yyjson_obj_get(health, "current"));
        mtmp->mhpmax = (short) yyjson_get_int(yyjson_obj_get(health, "maximum"));
    }

    mtmp->movement = yyjson_get_int(yyjson_obj_get(mon_val, "movement"));
    mtmp->female = yyjson_get_bool(yyjson_obj_get(mon_val, "female")) ? 1 : 0;
    mtmp->mpeaceful = yyjson_get_bool(yyjson_obj_get(mon_val, "peaceful")) ? 1 : 0;
    mtmp->mtame = (unsigned) yyjson_get_uint(yyjson_obj_get(mon_val, "tame"));
    if (mtmp->mtame) {
        newedog(mtmp);
    }
    mtmp->msleeping = yyjson_get_bool(yyjson_obj_get(mon_val, "sleeping")) ? 1 : 0;
    mtmp->mcanmove = yyjson_get_bool(yyjson_obj_get(mon_val, "canmove")) ? 1 : 0;
    mtmp->mcansee = 1;
    yyjson_val *cansee_val = yyjson_obj_get(mon_val, "cansee");
    if (cansee_val) {
        mtmp->mcansee = yyjson_get_bool(cansee_val) ? 1 : 0;
    }
    mtmp->mconf = yyjson_get_bool(yyjson_obj_get(mon_val, "confused")) ? 1 : 0;
    mtmp->mflee = yyjson_get_bool(yyjson_obj_get(mon_val, "fleeing")) ? 1 : 0;
    mtmp->minvis = yyjson_get_bool(yyjson_obj_get(mon_val, "invisible")) ? 1 : 0;
    mtmp->mtrapped = yyjson_get_bool(yyjson_obj_get(mon_val, "trapped")) ? 1 : 0;
    mtmp->cham = NON_PM;
    yyjson_val *cham_val = yyjson_obj_get(mon_val, "cham");
    if (cham_val) {
        mtmp->cham = (short) yyjson_get_int(cham_val);
    }

    /* Monster inventory */
    yyjson_val *inv = yyjson_obj_get(mon_val, "inventory");
    if (inv && yyjson_is_arr(inv)) {
        size_t idx, max;
        yyjson_val *oval;
        struct obj *prev_o = NULL;
        yyjson_arr_foreach(inv, idx, max, oval) {
            struct obj *mo = deserialize_c_object(oval, FALSE);
            if (mo) {
                mo->where = OBJ_MINVENT;
                mo->ocarry = mtmp;
                if (mo->owornmask & W_WEP) mtmp->mw = mo;
                if (!prev_o) {
                    mtmp->minvent = mo;
                } else {
                    prev_o->nobj = mo;
                }
                prev_o = mo;
            }
        }
    }

    return mtmp;
}

/* Restores game state from a Livesnap II JSON string */
int
restore_livesnap_from_string(const char *json_str, size_t len)
{
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *) json_str, len, 0, NULL, &err);
    if (!doc) {
        fprintf(stderr, "[livesnap] JSON parse error: %s at pos %zu\n", err.msg, err.pos);
        return 0;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *manifest = yyjson_obj_get(root, "manifest");
    yyjson_val *parts = yyjson_obj_get(root, "parts");
    if (!manifest || !parts) {
        fprintf(stderr, "[livesnap] Malformed Livesnap II: missing manifest or parts\n");
        yyjson_doc_free(doc);
        return 0;
    }

    const char *current_level = yyjson_get_str(yyjson_obj_get(manifest, "currentLevel"));
    if (!current_level) current_level = "level:1";

    yyjson_val *global = yyjson_obj_get(parts, "global");
    yyjson_val *lvl = yyjson_obj_get(parts, current_level);
    if (!global || !lvl) {
        fprintf(stderr, "[livesnap] Missing global or current level part: %s\n", current_level);
        yyjson_doc_free(doc);
        return 0;
    }

    /* 1. Restore time */
    yyjson_val *gtime = yyjson_obj_get(global, "time");
    if (gtime) {
        const char *mstr = yyjson_get_str(yyjson_obj_get(gtime, "moves"));
        if (mstr) svm.moves = atol(mstr);
    }

    /* Role & dungeon defaults if resuming from livesnap */
    if (flags.initrole >= 0 && flags.initrole < NUM_ROLES) {
        gu.urole = roles[flags.initrole];
    }
    if (flags.initrace >= 0 && flags.initrace < NUM_RACES) {
        gu.urace = races[flags.initrace];
    }
    init_livesnap_bases();
    quest_dnum = -1;
    mines_dnum = -1;
    sokoban_dnum = -1;
    tutorial_dnum = -1;
    svd.dungeon_topology.d_astral_level.dnum = -1;
    svd.dungeon_topology.d_astral_level.dlevel = -1;
    if (!svd.dungeons[0].dname[0]) {
        Strcpy(svd.dungeons[0].dname, "The Dungeons of Doom");
        svd.dungeons[0].entry_lev = 1;
        svd.dungeons[0].depth_start = 1;
        svd.dungeons[0].num_dunlevs = 30;
    }
    if (svn.n_dgns < 1) svn.n_dgns = 1;

    yyjson_val *qd = yyjson_obj_get(global, "quest_dnum");
    if (qd) {
        quest_dnum = (xint8) yyjson_get_int(qd);
    }
    yyjson_val *ql = yyjson_obj_get(global, "qlocate_level");
    if (ql) {
        qlocate_level.dnum = (xint8) yyjson_get_int(yyjson_obj_get(ql, "dnum"));
        qlocate_level.dlevel = (xint8) yyjson_get_int(yyjson_obj_get(ql, "dlevel"));
    }
    yyjson_val *dungs = yyjson_obj_get(global, "dungeons");
    if (dungs && yyjson_is_arr(dungs)) {
        size_t d_idx, d_max;
        yyjson_val *d_val;
        yyjson_arr_foreach(dungs, d_idx, d_max, d_val) {
            if (d_idx >= MAXDUNGEON) break;
            yyjson_val *dname = yyjson_obj_get(d_val, "dname");
            if (dname) {
                Strcpy(svd.dungeons[d_idx].dname, yyjson_get_str(dname));
            }
            yyjson_val *entry = yyjson_obj_get(d_val, "entry_lev");
            if (entry) svd.dungeons[d_idx].entry_lev = (xint16) yyjson_get_int(entry);
            yyjson_val *depth_start = yyjson_obj_get(d_val, "depth_start");
            if (depth_start) svd.dungeons[d_idx].depth_start = (int) yyjson_get_int(depth_start);
            yyjson_val *num_dunlevs = yyjson_obj_get(d_val, "num_dunlevs");
            if (num_dunlevs) svd.dungeons[d_idx].num_dunlevs = (xint16) yyjson_get_int(num_dunlevs);
            yyjson_val *reached = yyjson_obj_get(d_val, "dunlev_ureached");
            if (reached) svd.dungeons[d_idx].dunlev_ureached = (xint16) yyjson_get_int(reached);
            if ((int)(d_idx + 1) > svn.n_dgns) svn.n_dgns = (xint16)(d_idx + 1);
        }
    }

    /* Restore level identity */
    yyjson_val *lvl_id = yyjson_obj_get(lvl, "identity");
    if (lvl_id) {
        u.uz.dnum = (xint8) yyjson_get_int(yyjson_obj_get(lvl_id, "dnum"));
        u.uz.dlevel = (xint8) yyjson_get_int(yyjson_obj_get(lvl_id, "dlevel"));
        u.uz0.dnum = u.uz.dnum;
        u.uz0.dlevel = u.uz.dlevel;
    }

    /* 2. Restore Hero State */
    u.nv_range = 1;
    u.xray_range = -1;
    u.unblind_telepat_range = -1;
    yyjson_val *hero = yyjson_obj_get(global, "hero");
    if (hero) {
        yyjson_val *pos = yyjson_obj_get(hero, "position");
        if (pos) {
            u.ux = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 0));
            u.uy = (coordxy) yyjson_get_int(yyjson_arr_get(pos, 1));
        }

        yyjson_val *hp = yyjson_obj_get(hero, "health");
        if (hp) {
            u.uhp = yyjson_get_int(yyjson_obj_get(hp, "current"));
            u.uhpmax = yyjson_get_int(yyjson_obj_get(hp, "maximum"));
        }

        yyjson_val *en = yyjson_obj_get(hero, "energy");
        if (en) {
            u.uen = yyjson_get_int(yyjson_obj_get(en, "current"));
            u.uenmax = yyjson_get_int(yyjson_obj_get(en, "maximum"));
        }

        yyjson_val *exp = yyjson_obj_get(hero, "experience");
        if (exp) {
            u.ulevel = yyjson_get_int(yyjson_obj_get(exp, "currentLevel"));
        }

        yyjson_val *attrs = yyjson_obj_get(hero, "attributes");
        if (attrs) {
            static const char *anames[] = { "strength", "intelligence", "wisdom", "dexterity", "constitution", "charisma" };
            int a;
            for (a = 0; a < 6; a++) {
                yyjson_val *att = yyjson_obj_get(attrs, anames[a]);
                if (att) {
                    ABASE(a) = yyjson_get_int(yyjson_obj_get(att, "current"));
                    AMAX(a) = yyjson_get_int(yyjson_obj_get(att, "maximum"));
                }
            }
        }

        yyjson_val *luck = yyjson_obj_get(hero, "luck");
        if (luck) {
            u.uluck = (schar) yyjson_get_int(yyjson_obj_get(luck, "base"));
            u.moreluck = (schar) yyjson_get_int(yyjson_obj_get(luck, "carriedBonus"));
        }

        yyjson_val *hunger = yyjson_obj_get(hero, "hunger");
        if (hunger) {
            u.uhunger = yyjson_get_int(yyjson_obj_get(hunger, "nutrition"));
            yyjson_val *v_uhs = yyjson_obj_get(hunger, "state");
            if (v_uhs) {
                u.uhs = (unsigned) yyjson_get_int(v_uhs);
                if (u.uhs == FAINTED) {
                    ga.afternmv = unfaint;
                }
            }
        }
        u.ulycn = NON_PM;
        yyjson_val *lycn = yyjson_obj_get(hero, "lycanthropy");
        if (lycn) {
            u.ulycn = yyjson_get_int(lycn);
        }

        u.utrap = 0;
        u.utraptype = 0;
        yyjson_val *trapped_val = yyjson_obj_get(hero, "trapped");
        if (trapped_val) {
            u.utrap = (unsigned) yyjson_get_uint(yyjson_obj_get(trapped_val, "time"));
            u.utraptype = (uchar) yyjson_get_uint(yyjson_obj_get(trapped_val, "type"));
        }

        u.uinwater = 0;
        yyjson_val *uinwater_val = yyjson_obj_get(hero, "uinwater");
        if (uinwater_val && yyjson_get_bool(uinwater_val)) {
            u.uinwater = 1;
        }

        u.uundetected = 0;
        yyjson_val *uund_val = yyjson_obj_get(hero, "uundetected");
        if (uund_val && yyjson_get_bool(uund_val)) {
            u.uundetected = 1;
        }

        yyjson_val *polyd_val = yyjson_obj_get(hero, "polyd");
        if (polyd_val) {
            u.umonnum = yyjson_get_int(yyjson_obj_get(polyd_val, "monnum"));
            u.mh = yyjson_get_int(yyjson_obj_get(polyd_val, "mh"));
            u.mhmax = yyjson_get_int(yyjson_obj_get(polyd_val, "mhmax"));
            u.mtimedone = yyjson_get_int(yyjson_obj_get(polyd_val, "mtimedone"));
            set_uasmon();
        }

        yyjson_val *spells_val = yyjson_obj_get(hero, "spells");
        if (spells_val && yyjson_is_arr(spells_val)) {
            size_t idx, max;
            yyjson_val *sval;
            yyjson_arr_foreach(spells_val, idx, max, sval) {
                if (idx < MAXSPELL) {
                    svs.spl_book[idx].sp_id = (short) yyjson_get_int(yyjson_obj_get(sval, "id"));
                    svs.spl_book[idx].sp_lev = (xint16) yyjson_get_int(yyjson_obj_get(sval, "level"));
                    svs.spl_book[idx].sp_know = (int) yyjson_get_int(yyjson_obj_get(sval, "know"));
                }
            }
        }
        yyjson_val *smid = yyjson_obj_get(hero, "steed_mid");
        if (smid)
            u.usteed_mid = (unsigned) yyjson_get_uint(smid);
        else
            u.usteed_mid = 0;

        yyjson_val *halluc_v = yyjson_obj_get(hero, "hallucinating");
        if (halluc_v) {
            set_itimeout(&HHallucination, (long) yyjson_get_int(halluc_v));
        }
        yyjson_val *slimed_v = yyjson_obj_get(hero, "slimed");
        if (slimed_v) {
            set_itimeout(&Slimed, (long) yyjson_get_int(slimed_v));
        }

        /* Hero inventory */
        gi.invent = NULL;
        uwep = uarm = uarmc = uarmh = uarms = uarmg = uarmf = uarmu = uleft = uright = uamul = ublindf = uquiver = uswapwep = NULL;
        yyjson_val *inv = yyjson_obj_get(hero, "inventory");
        if (inv && yyjson_is_arr(inv)) {
            size_t idx, max;
            yyjson_val *oval;
            struct obj *prev_o = NULL;
            yyjson_arr_foreach(inv, idx, max, oval) {
                struct obj *o = deserialize_c_object(oval, FALSE);
                if (o) {
                    if (!prev_o) {
                        gi.invent = o;
                    } else {
                        prev_o->nobj = o;
                    }
                    prev_o = o;

                    /* Reconnect worn masks */
                    if (o->owornmask & W_WEP) uwep = o;
                    if (o->owornmask & W_SWAPWEP) uswapwep = o;
                    if (o->owornmask & W_QUIVER) uquiver = o;
                    if (o->owornmask & W_ARM) uarm = o;
                    if (o->owornmask & W_ARMC) uarmc = o;
                    if (o->owornmask & W_ARMH) uarmh = o;
                    if (o->owornmask & W_ARMS) uarms = o;
                    if (o->owornmask & W_ARMG) uarmg = o;
                    if (o->owornmask & W_ARMF) uarmf = o;
                    if (o->owornmask & W_ARMU) uarmu = o;
                    if (o->owornmask & W_RINGL) uleft = o;
                    if (o->owornmask & W_RINGR) uright = o;
                    if (o->owornmask & W_AMUL) uamul = o;
                    if (o->owornmask & W_TOOL) ublindf = o;
                    if (o->owornmask & W_BALL) uball = o;
                    if (o->owornmask & W_CHAIN) uchain = o;
                }
            }
        }
    }

    /* 3. Restore Map Grid */
    clear_level_structures();
    svl.level.flags.hero_memory = 1;
    yyjson_val *lvl_flags_in = yyjson_obj_get(lvl, "flags");
    if (lvl_flags_in) {
        if (yyjson_get_bool(yyjson_obj_get(lvl_flags_in, "sokoban_rules")))
            svl.level.flags.sokoban_rules = 1;
        if (yyjson_get_bool(yyjson_obj_get(lvl_flags_in, "hardfloor")))
            svl.level.flags.hardfloor = 1;
    }
    yyjson_val *map_obj = yyjson_obj_get(lvl, "map");
    if (map_obj) {
        yyjson_val *locations = yyjson_obj_get(map_obj, "locations");
        if (locations && yyjson_is_arr(locations)) {
            int x, y;
            for (x = 0; x < COLNO; x++) {
                yyjson_val *col = yyjson_arr_get(locations, (size_t) x);
                if (!col) continue;
                for (y = 0; y < ROWNO; y++) {
                    yyjson_val *cell = yyjson_arr_get(col, (size_t) y);
                    if (!cell) continue;
                    levl[x][y].typ = (schar) yyjson_get_int(yyjson_obj_get(cell, "typ"));
                    levl[x][y].lit = (uchar) yyjson_get_int(yyjson_obj_get(cell, "lit"));
                    levl[x][y].seenv = (uchar) yyjson_get_int(yyjson_obj_get(cell, "seen"));
                    levl[x][y].waslit = (uchar) yyjson_get_int(yyjson_obj_get(cell, "waslit"));
                    levl[x][y].flags = (uchar) yyjson_get_int(yyjson_obj_get(cell, "flags"));
                    levl[x][y].horizontal = (uchar) yyjson_get_int(yyjson_obj_get(cell, "horizontal"));
                }
            }
        }
    }

    /* 4. Restore Entities */
    yyjson_val *entities = yyjson_obj_get(lvl, "entities");
    if (entities) {
        /* Clear level monster and object grids */
        int x, y;
        for (x = 0; x < COLNO; x++) {
            for (y = 0; y < ROWNO; y++) {
                svl.level.monsters[x][y] = NULL;
                svl.level.objects[x][y] = NULL;
            }
        }

        /* Monsters */
        fmon = NULL;
        yyjson_val *mons_arr = yyjson_obj_get(entities, "monsters");
        if (mons_arr && yyjson_is_arr(mons_arr)) {
            size_t idx, max;
            yyjson_val *mval;
            struct monst *prev_m = NULL;
            yyjson_arr_foreach(mons_arr, idx, max, mval) {
                struct monst *mtmp = deserialize_c_monster(mval);
                if (mtmp) {
                    if (!prev_m) {
                        fmon = mtmp;
                    } else {
                        prev_m->nmon = mtmp;
                    }
                    prev_m = mtmp;
                    if (u.usteed_mid && mtmp->m_id == u.usteed_mid) {
                        u.usteed = mtmp;
                        u.usteed_mid = 0;
                    } else {
                        place_monster(mtmp, mtmp->mx, mtmp->my);
                    }
                }
            }
        }

        /* Floor Objects (place in reverse to preserve fobj and pile order) */
        fobj = NULL;
        yyjson_val *objs_arr = yyjson_obj_get(entities, "objects");
        if (objs_arr && yyjson_is_arr(objs_arr)) {
            size_t max = yyjson_arr_size(objs_arr);
            if (max > 0) {
                size_t i = max;
                while (i > 0) {
                    --i;
                    yyjson_val *oval = yyjson_arr_get(objs_arr, i);
                    struct obj *o = deserialize_c_object(oval, TRUE);
                    if (o) {
                        place_object(o, o->ox, o->oy);
                        if (o->owornmask & W_BALL) uball = o;
                        if (o->owornmask & W_CHAIN) uchain = o;
                    }
                }
            }
        }

        /* Traps (place in reverse to preserve list order) */
        gf.ftrap = NULL;
        yyjson_val *traps_arr = yyjson_obj_get(entities, "traps");
        if (traps_arr && yyjson_is_arr(traps_arr)) {
            size_t max = yyjson_arr_size(traps_arr);
            if (max > 0) {
                size_t i = max;
                while (i > 0) {
                    --i;
                    yyjson_val *tval = yyjson_arr_get(traps_arr, i);
                    struct trap *ttmp = newtrap();
                    if (ttmp) {
                        (void) memset((genericptr_t) ttmp, 0, sizeof(struct trap));
                        ttmp->tx = (coordxy) yyjson_get_int(yyjson_obj_get(tval, "tx"));
                        ttmp->ty = (coordxy) yyjson_get_int(yyjson_obj_get(tval, "ty"));
                        ttmp->ttyp = (schar) yyjson_get_int(yyjson_obj_get(tval, "ttyp"));
                        yyjson_val *ts = yyjson_obj_get(tval, "tseen");
                        ttmp->tseen = ts ? (yyjson_get_bool(ts) ? 1 : yyjson_get_int(ts) ? 1 : 0) : unhideable_trap(ttmp->ttyp);
                        yyjson_val *to = yyjson_obj_get(tval, "once");
                        if (to) ttmp->once = yyjson_get_bool(to) ? 1 : yyjson_get_int(to) ? 1 : 0;
                        yyjson_val *tm = yyjson_obj_get(tval, "madeby_u");
                        if (tm) ttmp->madeby_u = yyjson_get_bool(tm) ? 1 : yyjson_get_int(tm) ? 1 : 0;
                        yyjson_val *tl = yyjson_obj_get(tval, "launch");
                        if (tl && yyjson_is_obj(tl)) {
                            ttmp->launch.x = (coordxy) yyjson_get_int(yyjson_obj_get(tl, "x"));
                            ttmp->launch.y = (coordxy) yyjson_get_int(yyjson_obj_get(tl, "y"));
                        }
                        yyjson_val *tl2 = yyjson_obj_get(tval, "launch2");
                        if (tl2 && yyjson_is_obj(tl2)) {
                            ttmp->launch2.x = (coordxy) yyjson_get_int(yyjson_obj_get(tl2, "x"));
                            ttmp->launch2.y = (coordxy) yyjson_get_int(yyjson_obj_get(tl2, "y"));
                        }
                        ttmp->ntrap = gf.ftrap;
                        gf.ftrap = ttmp;
                    }
                }
            }
        }

        /* Engravings (place in reverse to preserve list order) */
        head_engr = NULL;
        yyjson_val *engr_arr = yyjson_obj_get(entities, "engravings");
        if (engr_arr && yyjson_is_arr(engr_arr)) {
            size_t max = yyjson_arr_size(engr_arr);
            if (max > 0) {
                size_t i = max;
                while (i > 0) {
                    --i;
                    yyjson_val *eval = yyjson_arr_get(engr_arr, i);
                    int ex = yyjson_get_int(yyjson_obj_get(eval, "tx"));
                    int ey = yyjson_get_int(yyjson_obj_get(eval, "ty"));
                    int etype = yyjson_get_int(yyjson_obj_get(eval, "etype"));
                    const char *etxt = yyjson_get_str(yyjson_obj_get(eval, "text"));
                    if (etxt) {
                        make_engr_at((coordxy) ex, (coordxy) ey, etxt, etxt, svm.moves, etype);
                    }
                }
            }
        }

        /* Stairs */
        gs.stairs = NULL;
        yyjson_val *stairs_arr = yyjson_obj_get(entities, "stairs");
        if (stairs_arr && yyjson_is_arr(stairs_arr)) {
            size_t idx, max;
            yyjson_val *sval;
            stairway *prev_s = NULL;
            yyjson_arr_foreach(stairs_arr, idx, max, sval) {
                stairway *st = (stairway *) alloc(sizeof(stairway));
                memset((genericptr_t) st, 0, sizeof(stairway));
                st->sx = (coordxy) yyjson_get_int(yyjson_obj_get(sval, "sx"));
                st->sy = (coordxy) yyjson_get_int(yyjson_obj_get(sval, "sy"));
                st->up = yyjson_get_bool(yyjson_obj_get(sval, "up")) ? 1 : 0;
                st->isladder = yyjson_get_bool(yyjson_obj_get(sval, "isladder")) ? 1 : 0;
                if (!prev_s) {
                    gs.stairs = st;
                } else {
                    prev_s->next = st;
                }
                prev_s = st;
            }
        }
    }

    yyjson_doc_free(doc);

    /* 5. Post-thaw Cache Reconstruction */
    program_state.something_worth_saving = 1;
    program_state.restoring = 0;
    {
        int i;
        for (i = 0; i < NUM_OBJECTS; i++)
            objects[i].oc_name_idx = objects[i].oc_descr_idx = i;
        for (i = MAXOCLASSES; i < NUM_OBJECTS; ++i) {
            if (!OBJ_DESCR(objects[i])) {
                objects[i].oc_name_known = 1;
            }
        }
    }
    if (u.umonnum <= 0 || u.umonnum >= NUMMONS) {
        u.umonnum = u.umonster = gu.urole.mnum;
    }
    set_uasmon();
    find_ac();
    calc_capacity(0);
    {
        struct monst *mtmp;
        for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
            if (mtmp->mtame) {
                initedog(mtmp, TRUE);
            }
        }
    }
    vision_reset();
    vision_recalc(0);
    newsym(u.ux, u.uy);
    see_monsters();
    see_objects();
    bot();
    flush_screen(1);


    return 1;
}

/* Restores game state from a Livesnap II JSON file */
int
restore_livesnap_from_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[livesnap] Cannot open file for reading: %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        return 0;
    }

    char *buf = (char *) alloc((size_t) sz + 1);
    size_t nr = fread(buf, 1, (size_t) sz, fp);
    fclose(fp);
    buf[nr] = '\0';

    int ok = restore_livesnap_from_string(buf, nr);
    free(buf);
    return ok;
}

/* Adjudication entry point */
int
run_livesnap_adjudication(const char *snap_file, const char *key_sequence, const char *out_snap_file)
{
    printf("[adjudicator] Loading snapshot: %s\n", snap_file);
    if (!restore_livesnap_from_file(snap_file)) {
        fprintf(stderr, "[adjudicator] FAILED to restore snapshot: %s\n", snap_file);
        return 1;
    }

    printf("[adjudicator] Restored successfully at moves=%ld, pos=(%d,%d), HP=%d/%d\n",
           (long) svm.moves, (int) u.ux, (int) u.uy, (int) u.uhp, (int) u.uhpmax);

    if (key_sequence && *key_sequence) {
        printf("[adjudicator] Executing %zu input keys...\n", strlen(key_sequence));
        const char *k;
        for (k = key_sequence; *k; k++) {
            rhack((int) *k);
        }
        printf("[adjudicator] Finished input sequence; moves=%ld, HP=%d\n", (long) svm.moves, (int) u.uhp);
    }

    if (out_snap_file && *out_snap_file) {
        printf("[adjudicator] Writing post-step snapshot to: %s\n", out_snap_file);
        if (!save_livesnap_to_file(out_snap_file)) {
            fprintf(stderr, "[adjudicator] FAILED to write output snapshot: %s\n", out_snap_file);
            return 2;
        }
    }

    return 0;
}
