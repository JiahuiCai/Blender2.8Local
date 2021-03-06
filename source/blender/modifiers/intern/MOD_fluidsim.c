/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 by the Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Daniel Dunbar
 *                 Ton Roosendaal,
 *                 Ben Batt,
 *                 Brecht Van Lommel,
 *                 Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_fluidsim.c
 *  \ingroup modifiers
 */


#include "DNA_scene_types.h"
#include "DNA_object_fluidsim.h"
#include "DNA_object_types.h"

#include "BLI_utildefines.h"


#include "BKE_cdderivedmesh.h"
#include "BKE_layer.h"
#include "BKE_modifier.h"

#include "DEG_depsgraph_build.h"

#include "MOD_fluidsim_util.h"
#include "MOD_modifiertypes.h"

#include "MEM_guardedalloc.h"

/* Fluidsim */
static void initData(ModifierData *md)
{
	FluidsimModifierData *fluidmd = (FluidsimModifierData *) md;
	
	fluidsim_init(fluidmd);
}
static void freeData(ModifierData *md)
{
	FluidsimModifierData *fluidmd = (FluidsimModifierData *) md;
	
	fluidsim_free(fluidmd);
}

static void copyData(ModifierData *md, ModifierData *target)
{
	FluidsimModifierData *fluidmd = (FluidsimModifierData *) md;
	FluidsimModifierData *tfluidmd = (FluidsimModifierData *) target;
	
	fluidsim_free(tfluidmd);

	if (fluidmd->fss) {
		tfluidmd->fss = MEM_dupallocN(fluidmd->fss);
		if (tfluidmd->fss && (tfluidmd->fss->meshVelocities != NULL)) {
			tfluidmd->fss->meshVelocities = MEM_dupallocN(tfluidmd->fss->meshVelocities);
		}
	}
}



static DerivedMesh *applyModifier(ModifierData *md, struct EvaluationContext *UNUSED(eval_ctx),
                                  Object *ob, DerivedMesh *dm,
                                  ModifierApplyFlag flag)
{
	FluidsimModifierData *fluidmd = (FluidsimModifierData *) md;
	DerivedMesh *result = NULL;
	
	/* check for alloc failing */
	if (!fluidmd->fss) {
		initData(md);
		
		if (!fluidmd->fss) {
			return dm;
		}
	}

	result = fluidsimModifier_do(fluidmd, md->scene, ob, dm, flag & MOD_APPLY_RENDER, flag & MOD_APPLY_USECACHE);

	return result ? result : dm;
}

static void updateDepsgraph(ModifierData *md,
                            struct Main *UNUSED(bmain),
                            struct Scene *scene,
                            Object *ob,
                            struct DepsNodeHandle *node)
{
	FluidsimModifierData *fluidmd = (FluidsimModifierData *) md;
	if (fluidmd && fluidmd->fss) {
		if (fluidmd->fss->type == OB_FLUIDSIM_DOMAIN) {
			FOREACH_SCENE_OBJECT(scene, ob1)
			{
				if (ob1 != ob) {
					FluidsimModifierData *fluidmdtmp =
					        (FluidsimModifierData *)modifiers_findByType(ob1, eModifierType_Fluidsim);

					/* Only put dependencies from NON-DOMAIN fluids in here. */
					if (fluidmdtmp && fluidmdtmp->fss && (fluidmdtmp->fss->type != OB_FLUIDSIM_DOMAIN)) {
						DEG_add_object_relation(node, ob1, DEG_OB_COMP_TRANSFORM, "Fluidsim Object");
					}
				}
			}
			FOREACH_SCENE_OBJECT_END
		}
	}
}

static bool dependsOnTime(ModifierData *UNUSED(md))
{
	return true;
}


ModifierTypeInfo modifierType_Fluidsim = {
	/* name */              "Fluidsim",
	/* structName */        "FluidsimModifierData",
	/* structSize */        sizeof(FluidsimModifierData),
	/* type */              eModifierTypeType_Nonconstructive,

	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_RequiresOriginalData |
	                        eModifierTypeFlag_Single,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     applyModifier,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        NULL,
	/* updateDepsgraph */   updateDepsgraph,
	/* dependsOnTime */     dependsOnTime,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
