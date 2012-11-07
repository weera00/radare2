/* radare - LGPL - Copyright 2011-2012 pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>
#include "dex/dex.h"

static int load(RBinArch *arch) {
	if(!(arch->bin_obj = r_bin_dex_new_buf(arch->buf)))
		return R_FALSE;
	return R_TRUE;
}

static ut64 baddr(RBinArch *arch) {
	return 0;
}

static int check(RBinArch *arch) {
	if (!arch->buf || !arch->buf->buf)
		return R_FALSE;
	if (!memcmp (arch->buf->buf, "dex\n035\0", 8))
		return R_TRUE;
	else if (!memcmp (arch->buf->buf, "dex\n009\0", 8)) // M3 (Nov-Dec 07)
		return R_TRUE;
	else if (!memcmp (arch->buf->buf, "dex\n009\0", 8)) // M5 (Feb-Mar 08)
		return R_TRUE;
	return R_FALSE;
}

static RBinInfo * info(RBinArch *arch) {
	char *version;
	RBinInfo *ret = R_NEW0 (RBinInfo);
	if (!ret) return NULL;
	strncpy (ret->file, arch->file, R_BIN_SIZEOF_STRINGS);
	strncpy (ret->rpath, "NONE", R_BIN_SIZEOF_STRINGS);
	strncpy (ret->type, "DEX CLASS", R_BIN_SIZEOF_STRINGS);
	ret->has_va = R_FALSE;
	version = r_bin_dex_get_version (arch->bin_obj);
	strncpy (ret->bclass, version, R_BIN_SIZEOF_STRINGS);
	free (version);
	strncpy (ret->rclass, "class", R_BIN_SIZEOF_STRINGS);
	strncpy (ret->os, "linux", R_BIN_SIZEOF_STRINGS);
	strncpy (ret->subsystem, "any", R_BIN_SIZEOF_STRINGS);
	strncpy (ret->machine, "Dalvik VM", R_BIN_SIZEOF_STRINGS);
	strncpy (ret->arch, "dalvik", R_BIN_SIZEOF_STRINGS);
	ret->lang = "java";
	ret->bits = 32;
	ret->big_endian = 0;
	ret->dbg_info = 1 | 4 | 8; /* Stripped | LineNums | Syms */
	return ret;
}

static RList* strings (RBinArch *arch) {
	RList *ret = NULL;
	RBinString *ptr = NULL;
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->bin_obj;
	ut32 i;
	char buf[6];
	int len;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	for (i = 0; i < bin->header.strings_size; i++) {
		if (!(ptr = R_NEW (RBinString)))
			break;
		r_buf_read_at (bin->b, bin->strings[i], (ut8*)&buf, 6);
		len = dex_read_uleb128 (buf);
		if (len>0 && len < R_BIN_SIZEOF_STRINGS) {
			r_buf_read_at (bin->b, bin->strings[i]+dex_uleb128_len (buf),
					(ut8*)&ptr->string, len);
			ptr->string[(int) len]='\0';
			ptr->rva = ptr->offset = bin->strings[i];
			ptr->size = len;
			ptr->ordinal = i+1;
			r_list_append (ret, ptr);
		} //else eprintf ("dex_read_uleb128: invalid read\n");
	}
	return ret;
}

static ut32 getmethodoffset (struct r_bin_dex_obj_t *bin, int n, ut32 *size) {
	ut32 mapsz, off = 0L;
	ut8 *buf, *map_end, *map;
	*size = 0;
	map = buf = malloc (bin->header.data_size);
	r_buf_read_at (bin->b, bin->header.data_offset, buf, bin->header.data_size);
	for (map_end = map+bin->header.data_size; map<map_end;) {
		int num = map[0] + (map[1]<<8);
		int ninsn = map[12] + (map[13]<<8);
		map += 16; // skip header
		mapsz = ninsn%2? (ninsn+1)*2: ninsn*2;
		if (n == num) {
			*size = mapsz;
			off = bin->header.data_offset + (size_t)(map - buf);
			break;
		}
		map += mapsz;
	}
	free (buf);
	return off;
}

static RList* methods (RBinArch *arch) {
	RList *ret = NULL;
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->bin_obj;
	int i, j, len;
	char *name, buf[6];
	RBinSymbol *ptr;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	for (i = 0; i<bin->header.method_size; i++) {
		if (!(ptr = R_NEW (RBinSymbol)))
			break;
		r_buf_read_at (bin->b, bin->strings[bin->methods[i].name_id], (ut8*)&buf, 6);
		len = dex_read_uleb128 (buf);

		name = malloc (len);
		if (!name) {
			eprintf ("error malloc string length %d\n", len);
			break;
		}
		r_buf_read_at (bin->b, bin->strings[bin->methods[i].name_id]+
				dex_uleb128_len (buf), (ut8*)name, len);
		snprintf (ptr->name, sizeof (ptr->name), "method.%d.%s", 
				bin->methods[i].class_id, name);
		free (name);
		strncpy (ptr->forwarder, "NONE", R_BIN_SIZEOF_STRINGS);
		strncpy (ptr->bind, "NONE", R_BIN_SIZEOF_STRINGS);
		ptr->ordinal = i+1;
		ptr->rva = ptr->offset = getmethodoffset (bin,
			(int)ptr->ordinal, (ut32*)&ptr->size);
		strncpy (ptr->type, ptr->rva? "FUNC":"IMPORT", R_BIN_SIZEOF_STRINGS);
		r_list_append (ret, ptr);
	}
	j = i;
	for (i = 0; i<bin->header.fields_size; i++) {
		if (!(ptr = R_NEW (RBinSymbol)))
			break;
		r_buf_read_at (bin->b, bin->strings[bin->fields[i].name_id], (ut8*)&buf, 6);

		len = dex_read_uleb128 (buf);
		name = malloc (len);
		if (!name) {
			eprintf ("error malloc string length %d\n", len);
			break;
		}
		r_buf_read_at (bin->b, bin->strings[bin->fields[i].name_id]+
				dex_uleb128_len (buf), (ut8*)name, len);
		snprintf (ptr->name, sizeof (ptr->name), "field.%d.%s", 
			bin->fields[i].class_id, name);
		free (name);

		strncpy (ptr->forwarder, "NONE", R_BIN_SIZEOF_STRINGS);
		strncpy (ptr->bind, "NONE", R_BIN_SIZEOF_STRINGS);
		strncpy (ptr->type, "FUNC", R_BIN_SIZEOF_STRINGS);
		ptr->rva = ptr->offset = bin->header.fields_offset +
			(sizeof (struct dex_field_t) * i);
		ptr->size = sizeof (struct dex_field_t);
		ptr->ordinal = j+i+1;
		r_list_append (ret, ptr);
	}
	return ret;
}

static void __r_bin_class_free(RBinClass *p) {
	r_bin_class_free (p);
}

static RList* classes (RBinArch *arch) {
	RBinClass *class;
	RList *ret = NULL;
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->bin_obj;
	struct dex_class_t entry;
	int i;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = (RListFree)__r_bin_class_free;
	for (i = 0; i < bin->header.class_size; i++) {
		r_buf_read_at (bin->b, (ut64) bin->header.class_offset
				+ (sizeof (struct dex_class_t)*i), (ut8*)&entry,
				sizeof (struct dex_class_t));
		// TODO: implement sections.. each section specifies a class boundary
{
		int len = 100;
		char *name = malloc (len);
		if (!name) {
			eprintf ("error malloc string length %d\n", len);
			break;
		}
		if (entry.source_file> bin->header.strings_size || entry.source_file<0)
			continue;
		r_buf_read_at (bin->b, bin->strings[entry.source_file],
				(ut8*)name, len);
		//snprintf (ptr->name, sizeof (ptr->name), "field.%s.%d", name, i);
		class = R_NEW0 (RBinClass);
		class->name = strdup (name[0]<0x41? name+1: name); // TODO: use RConstr here
		class->index = entry.class_id;
		r_list_append (ret, class);
#if VERBOSE
		eprintf ("class.%s=%d\n", name[0]==12?name+1:name, entry.class_id);
		eprintf ("# access_flags = %x;\n", entry.access_flags);
		eprintf ("# super_class = %d;\n", entry.super_class);
		eprintf ("# interfaces_offset = %08x;\n", entry.interfaces_offset);
		//eprintf ("ut32 source_file = %08x;\n", entry.source_file);
		eprintf ("# anotations_offset = %08x;\n", entry.anotations_offset);
		eprintf ("# class_data_offset = %08x;\n", entry.class_data_offset);
		eprintf ("# static_values_offset = %08x;\n\n", entry.static_values_offset);
#endif
		free (name);
}
	}
	return ret;
}

static RList* entries(RBinArch *arch) {
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->bin_obj;
	RBinAddr *ptr = R_NEW0 (RBinAddr);
	RList *ret = r_list_new ();
	ptr->offset = ptr->rva = bin->header.method_offset;
	r_list_append (ret, ptr);
	return ret;
}

//TODO
static int getoffset (RBinArch *arch, int type, int idx) {
	struct r_bin_dex_obj_t *dex = arch->bin_obj;
	switch (type) {
	case 'm': // methods
		if (dex->header.method_size > idx)
			return dex->header.method_offset+(sizeof (struct dex_method_t)*idx);
		break;
	case 'c': // class
		break;
	case 'f': // fields
		if (dex->header.fields_size > idx)
			return dex->header.fields_offset+(sizeof (struct dex_field_t)*idx);
		break;
	case 'o': // objects
		break;
	case 's': // strings
		if (dex->header.strings_size > idx)
			return dex->strings[idx];
		break;
	case 't': // things
		break;
	}
	return -1;
}

static RList* sections(RBinArch *arch) {
	RList *ret = NULL;
	RBinSection *ptr = NULL;
	struct r_bin_java_sym_t *s = NULL;
	RList *ml;
	RListIter *iter;

	int ns, fsymsz = 0;
	int fsym = 0;
	RBinSymbol *m;
	ml = methods (arch);
	r_list_foreach (ml, iter, m) {
		if (fsym == 0 || m->offset<fsym)
			fsym = m->offset;
		ns = m->offset + m->size;
		if (ns>fsymsz)
			fsymsz = ns;
	}
	if (fsym == 0)
		return NULL;
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	if ((ptr = R_NEW (RBinSection))) {
		strcpy (ptr->name, "code");
		ptr->size = ptr->vsize = fsymsz;
		ptr->offset = ptr->rva = fsym;
		ptr->srwx = 4|1;
		r_list_append (ret, ptr);
	}
	if ((ptr = R_NEW (RBinSection))) {
		strcpy (ptr->name, "constpool");
		ptr->size = ptr->vsize = fsym;
		ptr->offset = ptr->rva = 0;
		ptr->srwx = 4;
		r_list_append (ret, ptr);
	}
	if ((ptr = R_NEW (RBinSection))) {
		strcpy (ptr->name, "data");
		ptr->offset = ptr->rva = fsymsz+fsym;
		ptr->size = ptr->vsize = arch->buf->length - ptr->rva;
		ptr->srwx = 4|2;
		r_list_append (ret, ptr);
	}
	free (s);
	return ret;
}

struct r_bin_plugin_t r_bin_plugin_dex = {
	.name = "dex",
	.desc = "dex format bin plugin",
	.init = NULL,
	.fini = NULL,
	.load = &load,
	.destroy = NULL,
	.check = &check,
	.baddr = &baddr,
	.binsym = NULL,
	.entries = entries,
	.classes = classes,
	.sections = sections,
	.symbols = methods,
	.imports = NULL,
	.strings = strings,
	.info = &info,
	.fields = NULL,
	.libs = NULL,
	.relocs = NULL,
	.meta = NULL,
	.write = NULL,
	.get_offset = &getoffset
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_dex
};
#endif
