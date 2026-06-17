/*
  Copyright (C) 2026  StarField Xu
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <tar.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>

#include <sys/param.h>
#include <sys/statfs.h>
#include <sys/stat.h>

#include <libtar.h>
#include <libfdt.h>
#include <sha1.h>

#include "crc32.h"
#include "dtc/dtc.h"

#define OUTPUT_DIR "extract"

#define MAXPATHLEN_U 256 // (MAXPATHLEN/4) 

#define SNPCHK(_buf, _ret) \
	do {  \
	if ((_ret) < 0 || (_ret) >= sizeof(_buf)){  \
	      	fprintf(stderr, "[%s]%d:snpfintf over write\n", __func__, __LINE__);  \
		return -ENAMETOOLONG;  \
	}  \
	} while(0)

/*
 * Command line options
 */
int quiet;		/* Level of quietness */
unsigned int reservenum;/* Number of memory reservation slots */
int minsize;		/* Minimum blob size */
int padsize;		/* Additional padding to blob */
int alignsize;		/* Additional padding to blob according to the alignsize */
int phandle_format = PHANDLE_EPAPR;	/* Use linux,phandle or phandle properties */
int generate_symbols;	/* enable symbols & fixup support */
int generate_fixups;		/* suppress generation of fixups on symbol support */
int auto_label_aliases;		/* auto generate labels -> aliases */
int annotate;		/* Level of annotation: 1 for input source location
			   >1 for full input source location. */

int copy_file(const char *dest, const char *src) {
   FILE *fp_dest = fopen(dest, "wb");
   FILE *fp_src = fopen(src, "rb");
   if (!fp_dest || !fp_src) return -1;
   char buffer[1024];
   size_t bytes;
   while ((bytes = fread(buffer, 1, sizeof(buffer), fp_src)) > 0) {
       fwrite(buffer, 1, bytes, fp_dest);
   }
   fclose(fp_src);
   fclose(fp_dest);
   return 0;
}

char *get_file_from_path(char *path)
{
	char *p = strrchr(path, '/');
	return p+1;
}

int extract_fw(char *fw_path, char *kernel_path, char *root_path)
{
	int ret;
	char buf[MAXPATHLEN_U];
	char *filename;
	TAR *ptar = NULL;

	ret = tar_open(&ptar, fw_path, NULL, O_RDONLY, 0, TAR_GNU);
	if (ret)
	{
		fprintf(stderr, "Failed to open tar file: %s\n", strerror(errno));
		return -1;
	}

	while((ret = th_read(ptar)) == 0)
	{
		filename = th_get_pathname(ptar);
		if (ptar->options & TAR_VERBOSE)
			th_print_long_ls(ptar);
		
		char *p = strchr(filename, '/');
		if (p && strlen(p) > 1)
		{
			if (!strcmp(p + 1, "kernel"))
				snprintf(kernel_path, MAXPATHLEN_U, "%s/%s", OUTPUT_DIR, filename);

			if (!strcmp(p + 1, "root"))
				snprintf(root_path, MAXPATHLEN_U, "%s/%s", OUTPUT_DIR, filename);
		} else if(p && *(p+1)=='\0')
		{
			fprintf(stderr, "fw_name: %.*s\n", (int)strlen(filename)-1, filename);
		}
			
		snprintf(buf, sizeof(buf), "%s/%s", OUTPUT_DIR, filename);
		if ((ret = tar_extract_file(ptar, buf)) != 0)
		{
			fprintf(stderr, "Failed to extract file: %s : %s\n", buf, strerror(errno));
			break;
		}
	}

	ret = tar_close(ptar);
	if (ret)
	{
		fprintf(stderr, "Failed to close tar fd: %s\n", strerror(errno));
	}

	if (strlen(kernel_path) <= 0)
	{
		fprintf(stderr, "kernel not found\n");	
		return -1;
	}
	
	if (strlen(root_path) <= 0)
	{
		fprintf(stderr, "root not found\n");
		return -1;
	}

	return 0;
}

int extract_device_tree_blob(char *dtb_path)
{
	char buf[MAXPATHLEN_U];
	struct dt_info *dti;
	dti = dt_from_blob(dtb_path);
	
	strncpy(buf, dtb_path, strlen(dtb_path)-3);
	strcat(buf, "dts");

	FILE *outf = fopen(buf, "wb");
	if (!outf)
	{
		fprintf(stderr, "Failed to open File: %s: %s\n", buf, strerror(errno));
		return -1;
	}
	dt_to_source(outf, dti);
	fclose(outf);
		
	return 0;
}

int save_property_data(char *savepath, struct property *prop)
{
	
	FILE *outf = fopen(savepath, "wb");
	if (!outf)
	{
		fprintf(stderr, "Failed to open File: %s: %s\n", savepath, strerror(errno));
		return -1;
	}
	if (fwrite(prop->val.val, prop->val.len, 1, outf) < 0)
	{
		fprintf(stderr, "Failed to write data to File: %s: %s\n", savepath, strerror(errno));
		fclose(outf);
		return -1;	
	}
	fclose(outf);
	return 0;
}

int extract_kernel(char *k_path)
{
	int ret = 0;
	char basedir[MAXPATHLEN_U];
	char buf[MAXPATHLEN_U];
	char *p = strrchr(k_path, '/');
	if (p)
	{
		strncpy(basedir, k_path, p-k_path);
	}else
	{
		strcpy(basedir, ".");
	}
	ret = snprintf(buf, sizeof(buf), "%s/%s.its.tmp", basedir, p+1);
	SNPCHK(buf, ret);

	struct dt_info *dti;
	dti = dt_from_blob(k_path);
	
	FILE *outf = fopen(buf, "wb");
	if (!outf)
	{
		fprintf(stderr, "Failed to open File: %s: %s\n", buf, strerror(errno));
		ret = -1;
		goto err;
	}
	dt_to_source(outf, dti);
	fclose(outf);

	struct node *images, *i_child;
	struct property *prop;
	images = get_node_by_path(dti->dt, "/images");
	for_each_child(images, i_child)
	{
		if (!strncmp("kernel", i_child->name, strlen("kernel")))
		{

			prop = get_property(i_child, "data");
			if (!prop)
			{
				fprintf(stderr, "WARN: data property not found in %s\n", i_child->name);
				continue;
			}
			ret = snprintf(buf, MAXPATHLEN_U, "%s/%s.vmlinux", basedir, i_child->name);
			SNPCHK(buf, ret);

			if(save_property_data(buf, prop))
				continue;
		}
		else if (!strncmp("fdt", i_child->name, strlen("fdt")))
		{
			prop = get_property(i_child, "data");
			if (!prop)
			{
				fprintf(stderr, "WARN: data property not found in %s\n", i_child->name);
				continue;
			}
			ret = snprintf(buf, MAXPATHLEN_U, "%s/%s.dtb.tmp", basedir, i_child->name);
			SNPCHK(buf, ret);

			if(save_property_data(buf, prop))
				continue;
			extract_device_tree_blob(buf);
		}
	}

err:
	return ret;
}

int resum_hash(struct node *nd)
{
	struct node *child;
	struct property *value, *algo, *data;

	data = get_property(nd, "data");
	if (!data)
	{
		fprintf(stderr, "No data in node: %s\n", nd->name);
		return -1;
	}

	for_each_child(nd, child)
	{
		if (!strncmp(child->name, "hash", strlen("hash")))
		{
			algo = get_property(child, "algo");
			if (!algo)
			{
				fprintf(stderr, "Invalid hash node: %s\n", child->name);
				continue;
			}
			
			value = get_property(child, "value");
			if (!value)
			{
				fprintf(stderr, "Warning: property 'value' not found in hash node, the kernel file probably be broke\n");
				value = build_property("value", empty_data, NULL);
				add_property(child, value);
				value->val = data_add_marker(value->val, TYPE_UINT32, "value");
			} else
				value->val.len = 0; // mark data is empty;
			
			if (!strncmp(algo->val.val, "crc32", MIN(strlen(algo->val.val), strlen("crc32"))))
			{
				uint32_t crc = crc32(data->val.val, data->val.len);
				fprintf(stderr, "CRC32: 0x%08X\n", crc);
				fdt32_t fdtcrc = cpu_to_fdt32(crc);
				value->val = data_append_data(value->val, (void*)&fdtcrc, sizeof(fdtcrc));
			}else if (!strncmp(algo->val.val, "sha1", MIN(strlen(algo->val.val), strlen("sha1"))))
			{
				SHA1_CTX sha;
				uint8_t res[SHA1_DIGEST_LENGTH];
				SHA1Init(&sha);
				SHA1Update(&sha, (uint8_t *)data->val.val, data->val.len);
				SHA1Final(res, &sha);
				fprintf(stderr, "SHA1: 0x");
				for (int n=0; n<SHA1_DIGEST_LENGTH; n++)
					fprintf(stderr, "%02X", res[n]);
				fprintf(stderr, "\n");
				value->val = data_append_data(value->val, (void*)res, sizeof(res));
			} else {
				fprintf(stderr, "Unsupport hash mode: %s\n", algo->val.val);
			}
			// TODO: more hash support
		}
	
	}
	return 0;
}

int repack_kernel(char *orig_kernel_path)
{
	int ret;
	char basedir[MAXPATHLEN_U];
	char buf[MAXPATHLEN_U];
	char *p = strrchr(orig_kernel_path, '/');
	if (p)
	{
		strncpy(basedir, orig_kernel_path, p-orig_kernel_path);
	}else
	{
		strcpy(basedir, ".");
	}

	struct dt_info *dti;
	dti = dt_from_blob(orig_kernel_path);

	struct property *prop;	
	struct node *images, *i_child;
	images = get_node_by_path(dti->dt, "/images");
	for_each_child(images, i_child)
	{
		if (!strncmp("kernel", i_child->name, strlen("kernel")))
		{
			ret = snprintf(buf, sizeof(buf), "%s/%s.vmlinux", basedir, i_child->name);
			SNPCHK(buf, ret);
			fprintf(stderr, "buf: %s\n", buf);
			struct stat st;
			if (stat(buf, &st) < 0)
			{	
				fprintf(stderr, "Cannot get file stat %s: %s\n", get_file_from_path(buf), strerror(errno));
				continue;
			}

			FILE *f = fopen(buf, "rb");
			if (!f)
			{
				fprintf(stderr, "Cannot open file %s: %s\n", get_file_from_path(buf), strerror(errno));
				continue;
			}
			
			prop = get_property(i_child, "data");
			if (!prop)
			{
				fprintf(stderr, "Cannot get data property: %s\n", i_child->name);
				fclose(f);
				continue;
			}
			
			struct data dnew = data_copy_file(f, st.st_size);
			dnew = data_append_markers(dnew, prop->val.markers);
			prop->val.markers = NULL;
			data_free(prop->val);
			prop->val = dnew;
			fclose(f);

			resum_hash(i_child);
		}else if (!strncmp("fdt", i_child->name, strlen("fdt")))
		{
			
			ret = snprintf(buf, sizeof(buf), "%s/%s.dts", basedir, i_child->name);
			SNPCHK(buf, ret);

			struct stat st;
			if (stat(buf, &st) < 0)
			{	
				fprintf(stderr, "Cannot get file stat %s: %s\n", get_file_from_path(buf), strerror(errno));
				continue;
			}
			struct dt_info *dtdti;
			dtdti = dt_from_source(buf);

			ret = snprintf(buf, sizeof(buf), "%s/%s.new.dtb", basedir, i_child->name);
			SNPCHK(buf, ret);

			FILE *odtbf = fopen(buf, "wb");
			if (!odtbf)
			{
				fprintf(stderr, "Cannot open file %s: %s\n", get_file_from_path(buf), strerror(errno));
				continue;
			}
			dt_to_blob(odtbf, dtdti, DEFAULT_FDT_VERSION);
			fclose(odtbf);
			
			odtbf = fopen(buf, "rb");
			if (!odtbf)
			{
				fprintf(stderr, "Cannot open file %s: %s\n", get_file_from_path(buf), strerror(errno));
				continue;
			}

			prop = get_property(i_child, "data");
			if (!prop)
			{
				fprintf(stderr, "Cannot get data property: %s\n", i_child->name);
				fclose(odtbf);
				continue;
			}
			struct data dnew = data_copy_file(odtbf, st.st_size);
			dnew = data_append_markers(dnew, prop->val.markers);
			prop->val.markers = NULL;
			data_free(prop->val);
			prop->val = dnew;
			fclose(odtbf);

			resum_hash(i_child);
		}

	}


	ret = snprintf(buf, sizeof(buf), "%s/new_kernel.its", basedir);
	SNPCHK(buf, ret);

	FILE *outf = fopen(buf, "wb");
	if (!outf)
	{
		fprintf(stderr, "Failed to open File: %s: %s\n", buf, strerror(errno));
		return -1;
	}
	dt_to_source(outf, dti);
	fclose(outf);

	ret = snprintf(buf, sizeof(buf), "%s/new_kernel", basedir);
	SNPCHK(buf, ret);

	outf = fopen(buf, "wb");
	if (!outf)
	{
		fprintf(stderr, "Failed to open File: %s: %s\n", buf, strerror(errno));
		return -1;
	}
	dt_to_blob(outf, dti, DEFAULT_FDT_VERSION);
	fclose(outf);

	return 0;
}


int extract_all(char *fw_path)
{
	int ret;
	char root_path[MAXPATHLEN_U];
	char kernel_path[MAXPATHLEN_U];

	ret = mkdir(OUTPUT_DIR, 0777);
	if (ret == -1 && errno != EEXIST)
	{
		fprintf(stderr, "failed to create dir: %s: %s\n", OUTPUT_DIR, strerror(errno));
		return -1;
	}
	
	ret = mkdir(OUTPUT_DIR"/edit", 0777);
	if (ret == -1 && errno != EEXIST)
	{
		fprintf(stderr, "failed to create dir: %s: %s\n", OUTPUT_DIR"/edit", strerror(errno));
		return -1;
	}

	ret = extract_fw(fw_path, kernel_path, root_path);
	if (ret)
		return -1;
	printf("kernel: %s\nroot: %s\n", kernel_path, root_path);

	ret = copy_file(OUTPUT_DIR"/edit/kernel", kernel_path);
	if (ret)
	{
		fprintf(stderr, "Failed to copy kernel to edit dir: %s\n", strerror(errno));	
	}	
	
	ret = extract_kernel(OUTPUT_DIR"/edit/kernel");
	if (ret)
		return -1;
	return 0;
}

int repack_fw(char *fw_name)
{
	int ret;
	char buf[MAXPATHLEN_U];
	ret = repack_kernel(OUTPUT_DIR"/edit/kernel");
	if (ret)
	{
		fprintf(stderr, "Failed to repack kernel\n");
		return -1;
	}

	ret = snprintf(buf, sizeof(buf), OUTPUT_DIR"/%s", fw_name);
	SNPCHK(buf, ret);
	
	struct stat st;
	ret = lstat(buf, &st);
	if (ret)
	{
		fprintf(stderr, "Failed to get path stat: %s: %s\n", buf, strerror(errno));
		return -1;
	}
	if (!S_ISDIR(st.st_mode))
	{
		fprintf(stderr, "%s is not a dir\n", buf);
		return -1;
	}
	
	ret = snprintf(buf, sizeof(buf), OUTPUT_DIR"/%s/kernel", fw_name);
	SNPCHK(buf, ret);

	ret = copy_file(buf, OUTPUT_DIR"/edit/new_kernel");
	if (ret)
	{
		fprintf(stderr, "Failed to copy new kernel to %s: %s\n", buf, strerror(errno));
	}
	
	ret = snprintf(buf, sizeof(buf), OUTPUT_DIR"/%s-new.bin", fw_name);
	SNPCHK(buf, ret);
	
	TAR *t;
	ret = tar_open(&t, buf, NULL, O_WRONLY | O_CREAT, 0644, TAR_GNU);
	if (ret)
	{
		fprintf(stderr, "tar_open(): %s\n", strerror(errno));
		return -1;
	}

	fprintf(stderr, "new firmware: %s\n", buf);
	
	ret = snprintf(buf, sizeof(buf), OUTPUT_DIR"/%s", fw_name);
	SNPCHK(buf, ret);

	ret = tar_append_tree(t, buf, fw_name);
	if (ret)
	{
		fprintf(stderr,
			"tar_append_tree(\"%s\", \"%s\"): %s\n", buf,
			fw_name, strerror(errno));
			tar_close(t);
		return -1;
	}
	ret = tar_append_eof(t);
	if (ret)
	{
		fprintf(stderr, "tar_append_eof(): %s\n", strerror(errno));
		tar_close(t);
		return -1;
	}

	ret = tar_close(t);
	if (ret)
	{
		fprintf(stderr, "tar_close(): %s\n", strerror(errno));
		return -1;
	}
	

	return 0;	
}

void print_usage(char *arg0)
{
	fprintf(stderr, 
		"%s: a simple to to unpack openwrt sysupgrde.bin file\n"\
		"e <sysupgrade.bin>    unpack firmware\n"\
		"c <fw_name>           repack firmware"
		, arg0);
}

int main(int argc, char *argv[])
{

	if (argc < 3)
	{
		fprintf(stderr, "missing arguments\n");
		print_usage(argv[0]);
		return 1;
	}
	
	if (*(argv[1]) == 'e')
		extract_all(argv[2]);
	else if(*(argv[1]) == 'c')
		repack_fw(argv[2]);
	else
		print_usage(argv[0]);
	
	return 0;
}
