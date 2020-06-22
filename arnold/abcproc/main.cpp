#include <ai.h>
#include <dso.h>
#include <visitors.h>
#include <globallock.h>


#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
inline void millisleep(unsigned long milliseconds)
{
   Sleep(milliseconds);
}
#else
#  include <unistd.h>
inline void millisleep(unsigned long milliseconds)
{
   usleep(milliseconds * 1000);
}
#endif


// ---

AI_PROCEDURAL_NODE_EXPORT_METHODS(AbcProcMtd);


node_parameters
{
   // Common parameters
   AiParameterStr(Strings::filename, Strings::_empty);
   AiParameterStr(Strings::objectpath, Strings::_empty);
   AiParameterStr(Strings::excludepath, Strings::_empty);
   AiParameterStr(Strings::nameprefix, Strings::_empty);

   // use node's own 'motion_start' and 'motion_end' parameters
   AiParameterFlt(Strings::frame, 0.0f);
   AiParameterFlt(Strings::fps, 0.0f);
   AiParameterEnum(Strings::cycle, CT_hold, CycleTypeNames);
   AiParameterFlt(Strings::start_frame, std::numeric_limits<float>::max());
   AiParameterFlt(Strings::end_frame, -std::numeric_limits<float>::max());
   AiParameterFlt(Strings::speed, 1.0f);
   AiParameterFlt(Strings::offset, 0.0f);
   AiParameterBool(Strings::preserve_start_frame, false);

   AiParameterUInt(Strings::samples, 1);
   AiParameterUInt(Strings::expand_samples_iterations, 0);
   AiParameterBool(Strings::optimize_samples, false);

   AiParameterBool(Strings::ignore_deform_blur, false);
   AiParameterBool(Strings::ignore_transform_blur, false);
   AiParameterBool(Strings::ignore_visibility, false);
   AiParameterBool(Strings::ignore_transforms, false);
   AiParameterBool(Strings::ignore_instances, false);
   AiParameterBool(Strings::ignore_nurbs, false);

   AiParameterFlt(Strings::velocity_scale, 1.0f);
   AiParameterStr(Strings::velocity_name, Strings::_empty);
   AiParameterStr(Strings::acceleration_name, Strings::_empty);
   AiParameterBool(Strings::force_velocity_blur, false)

   AiParameterBool(Strings::ignore_reference, false);
   AiParameterEnum(Strings::reference_source, RS_attributes, ReferenceSourceNames);
   AiParameterStr(Strings::reference_position_name, Strings::Pref);
   AiParameterStr(Strings::reference_normal_name, Strings::Nref);
   AiParameterFlt(Strings::reference_frame, 0.0f);

   AiParameterEnum(Strings::attributes_evaluation_time, AET_render, AttributesEvaluationTimeNames);
   // Only applies to attributes read from the .abc files, not attributes already existing on arnold node
   AiParameterArray(Strings::remove_attribute_prefices, AiArray(0, 1, AI_TYPE_STRING));
   // Names in 'ignore_attributes' and 'force_constant_attributes' are without any prefix to remove
   AiParameterArray(Strings::ignore_attributes, AiArray(0, 1, AI_TYPE_STRING));
   // This only applies to GeoParams contained in alembic file
   AiParameterArray(Strings::force_constant_attributes, AiArray(0, 1, AI_TYPE_STRING));

   AiParameterArray(Strings::compute_tangents_for_uvs, AiArray(0, 1, AI_TYPE_STRING));

   AiParameterStr(Strings::radius_name, Strings::_empty);
   AiParameterFlt(Strings::radius_min, 0.0f);
   AiParameterFlt(Strings::radius_max, 1000000.0f);
   AiParameterFlt(Strings::radius_scale, 1.0f);

   AiParameterFlt(Strings::width_min, 0.0f);
   AiParameterFlt(Strings::width_max, 1000000.0f);
   AiParameterFlt(Strings::width_scale, 1.0f);

   AiParameterUInt(Strings::nurbs_sample_rate, 5);

   // Others
   AiParameterBool(Strings::verbose, false);
   AiParameterStr(Strings::rootdrive, Strings::_empty);

   AiMetaDataSetBool(nentry, Strings::filename, Strings::filepath, true);
}

procedural_init
{
   AiMsgDebug("[abcproc] ProcInit [thread %p]", AiThreadSelf());
   Dso *dso = new Dso(node);
   *user_ptr = dso;
   return 1;
}

procedural_num_nodes
{
   AiMsgDebug("[abcproc] ProcNumNodes [thread %p]", AiThreadSelf());
   Dso *dso = (Dso*) user_ptr;

   return int(dso->numShapes());
}

procedural_get_node
{
   // This function won't get call for the same procedural node from different threads

   AiMsgDebug("[abcproc] ProcGetNode [thread %p]", AiThreadSelf());
   Dso *dso = (Dso*) user_ptr;

   if (i == 0)
   {
      // generate the shape(s)
      if (dso->mode() == PM_multi)
      {
         // only generates new procedural nodes (read transform and bound information)
         MakeProcedurals procNodes(dso);
         dso->scene()->visit(AlembicNode::VisitDepthFirst, procNodes);

         if (procNodes.numNodes() != dso->numShapes())
         {
            AiMsgWarning("[abcproc] %lu procedural(s) generated (%lu expected)", procNodes.numNodes(), dso->numShapes());
         }

         for (size_t i=0; i<dso->numShapes(); ++i)
         {
            AtNode *node = procNodes.node(i);
            Alembic::Abc::M44d W;
            AtMatrix mtx;

            dso->setGeneratedNode(i, node);
         }
      }
      else
      {
         AtNode *output = 0;
         AtNode *master = 0;

         AbcProcGlobalLock::Acquire();
         bool isInstance = dso->isInstance(&master);
         if (!isInstance)
         {
            // temporarily set master node to procedural to avoid concurrent expansion
            dso->setMasterNode(dso->procNode());
         }
         else
         {
            while (AiNodeGetNodeEntry(master) == AiNodeGetNodeEntry(dso->procNode()))
            {
               // master is an abcproc node, it means it hasnt been expanded yet
               // release lock and wait 10 milliseconds before re-acquiring it
               AbcProcGlobalLock::Release();
               millisleep(10);
               AbcProcGlobalLock::Acquire();
               master = dso->masterNode();
            }
         }
         AbcProcGlobalLock::Release();

         if (!isInstance)
         {
            MakeShape visitor(dso);
            // dso->scene()->visit(AlembicNode::VisitFilteredFlat, visitor);
            dso->scene()->visit(AlembicNode::VisitDepthFirst, visitor);

            output = visitor.node();

            if (output)
            {
               dso->transferShapeParams(output);

               dso->transferUserParams(output);

               AbcProcGlobalLock::Acquire();

               dso->setMasterNode(output);

               AbcProcGlobalLock::Release();
            }
         }
         else
         {
            if (dso->verbose())
            {
               AiMsgInfo("[abcproc] Create a new instance of \"%s\"", AiNodeGetName(master));
            }

            {
#ifdef SAFE_NODE_CREATE
               AbcProcScopeLock _lock;
#endif
               // rename source procedural node
               std::string name = AiNodeGetName(dso->procNode());
               AiNodeSetStr(dso->procNode(), Strings::name, dso->uniqueName("_" + name).c_str());

               // use procedural name for newly generated instance
               output = AiNode(Strings::ginstance, name.c_str(), dso->procNode());
            }

            AiNodeSetBool(output, Strings::inherit_xform, false);
            AiNodeSetPtr(output, Strings::node, master);

            dso->transferShapeParams(output);

            dso->transferUserParams(output);
         }

         dso->setGeneratedNode(0, output);
      }
   }

   return dso->generatedNode(i);
}

procedural_cleanup
{
   AiMsgDebug("[abcproc] ProcCleanup [thread %p]", AiThreadSelf());
   Dso *dso = (Dso*) user_ptr;
   delete dso;
   return 1;
}

// ---

#ifdef ALEMBIC_WITH_HDF5

#include <H5public.h>

#ifdef H5_HAVE_THREADSAFE

#ifdef _WIN32

// ToDo

#else // _WIN32

#include <pthread.h>

static void __attribute__((constructor)) on_dlopen(void)
{
   H5dont_atexit();
}

static void __attribute__((destructor)) on_dlclose(void)
{
   // Hack around thread termination segfault
   // -> alembic procedural is unloaded before thread finishes
   //    resulting on HDF5 keys to leak

   extern pthread_key_t H5TS_errstk_key_g;
   extern pthread_key_t H5TS_funcstk_key_g;
   extern pthread_key_t H5TS_cancel_key_g;

   pthread_key_delete(H5TS_cancel_key_g);
   pthread_key_delete(H5TS_funcstk_key_g);
   pthread_key_delete(H5TS_errstk_key_g);
}

#endif // _WIN32

#endif // H5_HAVE_THREADSAFE

#endif // ALEMBIC_WITH_HDF5


node_loader
{
   if (i > 0)
   {
      return false;
   }

   node->methods = AbcProcMtd;
   node->output_type = AI_TYPE_NONE;
   node->name = "abcproc";
   node->node_type = AI_NODE_SHAPE_PROCEDURAL;
   strcpy(node->version, AI_VERSION);

   return true;
}
