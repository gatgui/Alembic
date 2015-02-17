#include "AbcShape.h"
#include "AlembicSceneVisitors.h"
#include "AlembicSceneCache.h"

#include <maya/MFnTypedAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPoint.h>
#include <maya/MMatrix.h>
#include <maya/MFileObject.h>
#include <maya/MDrawData.h>
#include <maya/MDrawRequest.h>
#include <maya/MMaterial.h>
#include <maya/MDagPath.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MFnStringData.h>
#include <maya/MSelectionMask.h>
#include <maya/MFnCamera.h>
#include <maya/MSelectionList.h>
#include <maya/MDGModifier.h>
#include <maya/MGlobal.h>

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#include <vector>
#include <map>

#ifdef ABCSHAPE_VRAY_SUPPORT

#include <maya/MItDependencyNodes.h>
#include <maya/MFnSet.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlugArray.h>
#include <maya/MSyntax.h>
#include <maya/MArgParser.h>
#include <set>

class MyMFnSet : public MFnSet
{
public:
   
   MyMFnSet()
      : MFnSet()
   {
   }
   
   MyMFnSet(MObject &object, MStatus *stat=NULL)
      : MFnSet(object, stat), mHandle(object)
   {
   }
   
   MyMFnSet(const MObject &object, MStatus *stat=NULL)
      : MFnSet(object, stat), mHandle(object)
   {
   }
   
   MyMFnSet(const MyMFnSet &rhs)
      : MFnSet()
   {
      operator=(rhs);
   }
   
   virtual ~MyMFnSet()
   {
   }
   
   MyMFnSet& operator=(const MyMFnSet &rhs)
   {
      if (this != &rhs)
      {
         MObject rhsObj = rhs.object();
         
         mHandle = rhsObj;
         setObject(rhsObj);
      }
      return *this;
   }
   
   bool operator<(const MyMFnSet &rhs) const
   {
      return (mHandle.hashCode() < rhs.mHandle.hashCode());
   }
   
   MyMFnSet* operator&()
   {
      return this;
   }
   
   const MyMFnSet* operator&() const
   {
      return this;
   }

private:
   
   MObjectHandle mHandle;
};

typedef std::set<MyMFnSet> MyMFnSetSet;

// ---

AbcShapeVRayDisp::DispTexMap AbcShapeVRayDisp::DispTexs;

void* AbcShapeVRayDisp::create()
{
   return new AbcShapeVRayDisp();
}

MSyntax AbcShapeVRayDisp::createSyntax()
{
   MSyntax syntax;
   
   syntax.addFlag("-r", "-reset", MSyntax::kNoArg);
   syntax.addFlag("-dl", "-displist", MSyntax::kNoArg);
   syntax.addFlag("-d", "-disp", MSyntax::kString);
   syntax.addFlag("-f", "-float", MSyntax::kNoArg);
   syntax.addFlag("-c", "-color", MSyntax::kNoArg);
   
   syntax.setMinObjects(0);
   syntax.setMaxObjects(0);
   syntax.enableQuery(false);
   syntax.enableEdit(false);
   
   return syntax;
}

AbcShapeVRayDisp::AbcShapeVRayDisp()
   : MPxCommand()
{
}
   
AbcShapeVRayDisp::~AbcShapeVRayDisp()
{
}

bool AbcShapeVRayDisp::hasSyntax() const
{
   return true;
}
   
bool AbcShapeVRayDisp::isUndoable() const
{
   return false;
}

MStatus AbcShapeVRayDisp::doIt(const MArgList& args)
{
   MStatus status;
   
   MArgParser argData(syntax(), args, &status);
   
   if (argData.isFlagSet("reset"))
   {
      DispTexs.clear();
      return MS::kSuccess;
   }
   else
   {
      if (argData.isFlagSet("displist"))
      {
         MStringArray names;
         
         for (DispTexMap::iterator it = DispTexs.begin(); it != DispTexs.end(); ++it)
         {
            names.append(it->first.c_str());
         }
         
         setResult(names);
         
         return MS::kSuccess;
      }
      else if (!argData.isFlagSet("disp"))
      {
         MGlobal::displayError("Either -disp or -displist flag must be set");
         return MS::kFailure;
      }
      else if (!argData.isFlagSet("color") && !argData.isFlagSet("float"))
      {
         MGlobal::displayError("Either -color or -float flag must be set");
         return MS::kFailure;
      }
      else
      {
         MString val;
         
         status = argData.getFlagArgument("disp", 0, val);
         if (status != MS::kSuccess)
         {
            MGlobal::displayError("Invalid valud for -disp flag (" + val + ")");
            return status;
         }
         
         DispTexMap::iterator it = DispTexs.find(val.asChar());
         
         MStringArray names;
         
         if (it != DispTexs.end())
         {
            DispShapes &dispShapes = it->second;
            
            if (argData.isFlagSet("color"))
            {
               if (argData.isFlagSet("float"))
               {
                  MGlobal::displayError("-float/-color cannot be used at the same time");
                  return MS::kFailure;
               }
               
               for (NameSet::iterator nit = dispShapes.asColor.begin(); nit != dispShapes.asColor.end(); ++nit)
               {
                  names.append(nit->c_str());
               }
            }
            else
            {
               for (NameSet::iterator nit = dispShapes.asFloat.begin(); nit != dispShapes.asFloat.end(); ++nit)
               {
                  names.append(nit->c_str());
               }
            }
         }
         
         setResult(names);
         
         return MS::kSuccess;
      }
   }
}

#endif

#define MCHECKERROR(STAT,MSG)                   \
    if (!STAT) {                                \
        perror(MSG);                            \
        return MS::kFailure;                    \
    }


const MTypeId AbcShape::ID(0x00082698);
MCallbackId AbcShape::CallbackID;
MObject AbcShape::aFilePath;
MObject AbcShape::aObjectExpression;
MObject AbcShape::aDisplayMode;
MObject AbcShape::aTime;
MObject AbcShape::aSpeed;
MObject AbcShape::aPreserveStartFrame;
MObject AbcShape::aStartFrame;
MObject AbcShape::aEndFrame;
MObject AbcShape::aOffset;
MObject AbcShape::aCycleType;
MObject AbcShape::aIgnoreXforms;
MObject AbcShape::aIgnoreInstances;
MObject AbcShape::aIgnoreVisibility;
MObject AbcShape::aNumShapes;
MObject AbcShape::aPointWidth;
MObject AbcShape::aLineWidth;
MObject AbcShape::aDrawTransformBounds;
MObject AbcShape::aDrawLocators;
MObject AbcShape::aOutBoxMin;
MObject AbcShape::aOutBoxMax;
MObject AbcShape::aAnimated;
#ifdef ABCSHAPE_VRAY_SUPPORT
MObject AbcShape::aOutApiType;
MObject AbcShape::aVRayGeomResult;
MObject AbcShape::aVRayGeomInfo;
#endif

void* AbcShape::creator()
{
   return new AbcShape();
}

MStatus AbcShape::initialize()
{
   MStatus stat;
   MFnTypedAttribute tAttr;
   MFnEnumAttribute eAttr;
   MFnUnitAttribute uAttr;
   MFnNumericAttribute nAttr;
   
   MFnStringData filePathDefault;
   MObject filePathDefaultObject = filePathDefault.create("");
   aFilePath = tAttr.create("filePath", "fp", MFnData::kString, filePathDefaultObject, &stat);
   MCHECKERROR(stat, "Could not create 'filePath' attribute");
   tAttr.setInternal(true);
   tAttr.setUsedAsFilename(true);
   stat = addAttribute(aFilePath);
   MCHECKERROR(stat, "Could not add 'filePath' attribute");
   
   aObjectExpression = tAttr.create("objectExpression", "oe", MFnData::kString, MObject::kNullObj, &stat);
   MCHECKERROR(stat, "Could not create 'objectExpression' attribute");
   tAttr.setInternal(true);
   stat = addAttribute(aObjectExpression);
   MCHECKERROR(stat, "Could not add 'objectExpression' attribute");
   
   aDisplayMode = eAttr.create("displayMode", "dm");
   MCHECKERROR(stat, "Could not create 'displayMode' attribute");
   eAttr.addField("Box", DM_box);
   eAttr.addField("Boxes", DM_boxes);
   eAttr.addField("Points", DM_points);
   eAttr.addField("Geometry", DM_geometry);
   eAttr.setInternal(true);
   stat = addAttribute(aDisplayMode);
   MCHECKERROR(stat, "Could not add 'displayMode' attribute");
   
   aTime = uAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
   MCHECKERROR(stat, "Could not create 'time' attribute");
   uAttr.setStorable(true);
   uAttr.setInternal(true);
   stat = addAttribute(aTime);
   MCHECKERROR(stat, "Could not add 'time' attribute");
   
   aSpeed = nAttr.create("speed", "sp", MFnNumericData::kDouble, 1.0, &stat);
   MCHECKERROR(stat, "Could not create 'speed' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aSpeed);
   MCHECKERROR(stat, "Could not add 'speed' attribute");
   
   aPreserveStartFrame = nAttr.create("preserveStartFrame", "psf", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'preserveStartFrame' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(false);
   nAttr.setInternal(true);
   stat = addAttribute(aPreserveStartFrame);
   MCHECKERROR(stat, "Could not add 'preserveStartFrame' attribute");

   aOffset = nAttr.create("offset", "of", MFnNumericData::kDouble, 0, &stat);
   MCHECKERROR(stat, "Could not create 'offset' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aOffset);
   MCHECKERROR(stat, "Could not add 'offset' attribute");

   aCycleType = eAttr.create("cycleType", "ct", 0,  &stat);
   MCHECKERROR(stat, "Could not create 'cycleType' attribute");
   eAttr.addField("Hold", CT_hold);
   eAttr.addField("Loop", CT_loop);
   eAttr.addField("Reverse", CT_reverse);
   eAttr.addField("Bounce", CT_bounce);
   eAttr.setWritable(true);
   eAttr.setStorable(true);
   eAttr.setKeyable(true);
   eAttr.setInternal(true);
   stat = addAttribute(aCycleType);
   MCHECKERROR(stat, "Could not add 'cycleType' attribute");
   
   aStartFrame = nAttr.create("startFrame", "sf", MFnNumericData::kDouble, 1.0, &stat);
   MCHECKERROR(stat, "Could not create 'startFrame' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aStartFrame);
   MCHECKERROR(stat, "Could not add 'startFrame' attribute");

   aEndFrame = nAttr.create("endFrame", "ef", MFnNumericData::kDouble, 1.0, &stat);
   MCHECKERROR(stat, "Could not create 'endFrame' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aEndFrame);
   MCHECKERROR(stat, "Could not add 'endFrame' attribute");
   
   aIgnoreXforms = nAttr.create("ignoreXforms", "ixf", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'ignoreXforms' attribute")
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aIgnoreXforms);
   MCHECKERROR(stat, "Could not add 'ignoreXforms' attribute");
   
   aIgnoreInstances = nAttr.create("ignoreInstances", "iin", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'ignoreInstances' attribute")
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aIgnoreInstances);
   MCHECKERROR(stat, "Could not add 'ignoreInstances' attribute");
   
   aIgnoreVisibility = nAttr.create("ignoreVisibility", "ivi", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'ignoreVisibility' attribute")
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aIgnoreVisibility);
   MCHECKERROR(stat, "Could not add 'ignoreVisibility' attribute");
   
   aNumShapes = nAttr.create("numShapes", "nsh", MFnNumericData::kLong, 0, &stat);
   nAttr.setWritable(false);
   nAttr.setStorable(false);
   stat = addAttribute(aNumShapes);
   MCHECKERROR(stat, "Could not add 'numShapes' attribute");
   
   aPointWidth = nAttr.create("pointWidth", "ptw", MFnNumericData::kFloat, 0.0, &stat);
   MCHECKERROR(stat, "Could not create 'pointWidth' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aPointWidth);
   MCHECKERROR(stat, "Could not add 'pointWidth' attribute");
   
   aLineWidth = nAttr.create("lineWidth", "lnw", MFnNumericData::kFloat, 0.0, &stat);
   MCHECKERROR(stat, "Could not create 'lineWidth' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aLineWidth);
   MCHECKERROR(stat, "Could not add 'lineWidth' attribute");
   
   aDrawTransformBounds = nAttr.create("drawTransformBounds", "dtb", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'drawTransformBounds' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aDrawTransformBounds);
   MCHECKERROR(stat, "Could not add 'drawTransformBounds' attribute");
   
   aDrawLocators = nAttr.create("drawLocators", "dl", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'drawLocators' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setKeyable(true);
   nAttr.setInternal(true);
   stat = addAttribute(aDrawLocators);
   MCHECKERROR(stat, "Could not add 'drawLocators' attribute");
   
   aOutBoxMin = nAttr.create("outBoxMin", "obmin", MFnNumericData::k3Double, 0.0, &stat);
   MCHECKERROR(stat, "Could not create 'outBoxMin' attribute");
   nAttr.setWritable(false);
   nAttr.setStorable(false);
   stat = addAttribute(aOutBoxMin);
   MCHECKERROR(stat, "Could not add 'outBoxMin' attribute");
   
   aOutBoxMax = nAttr.create("outBoxMax", "obmax", MFnNumericData::k3Double, 0.0, &stat);
   MCHECKERROR(stat, "Could not create 'outBoxMax' attribute");
   nAttr.setWritable(false);
   nAttr.setStorable(false);
   stat = addAttribute(aOutBoxMax);
   MCHECKERROR(stat, "Could not add 'outBoxMax' attribute");
   
   aAnimated = nAttr.create("animated", "anm", MFnNumericData::kBoolean, 0, &stat);
   MCHECKERROR(stat, "Could not create 'animated' attribute");
   nAttr.setWritable(true);
   nAttr.setStorable(true);
   nAttr.setHidden(true);
   stat = addAttribute(aAnimated);
   MCHECKERROR(stat, "Could not add 'animated' attribute");
   
#ifdef ABCSHAPE_VRAY_SUPPORT
   MFnStringData outApiTypeDefault;
   MObject outApiTypeDefaultObject = outApiTypeDefault.create("VRayGeometry");
   aOutApiType = tAttr.create("outApiType", "oat", MFnData::kString, outApiTypeDefaultObject, &stat);
   tAttr.setKeyable(false);
   tAttr.setStorable(true);
   tAttr.setHidden(true);
   MCHECKERROR(stat, "Could not create 'outApiType' attribute");
   stat = addAttribute(aOutApiType);
   MCHECKERROR(stat, "Could not add 'outApiType' attribute");
   
   aVRayGeomInfo = nAttr.createAddr("vrayGeomInfo", "vgi", &stat);
   MCHECKERROR(stat, "Could not create 'vrayGeomInfo' attribute");;
   nAttr.setKeyable(false);
   nAttr.setStorable(false);
   nAttr.setReadable(true);
   nAttr.setWritable(true);
   nAttr.setHidden(true);
   stat = addAttribute(aVRayGeomInfo);
   MCHECKERROR(stat, "Could not add 'vrayGeomInfo' attribute");;
   
   aVRayGeomResult = nAttr.create("vrayGeomResult", "vgr", MFnNumericData::kInt, 0, &stat);
   MCHECKERROR(stat, "Could not create 'vrayGeomResult' attribute");
   nAttr.setKeyable(false);
   nAttr.setStorable(false);
   nAttr.setReadable(true);
   nAttr.setWritable(false);
   nAttr.setHidden(true);
   stat = addAttribute(aVRayGeomResult);
   MCHECKERROR(stat, "Could not add 'vrayGeomResult' attribute");
   
   attributeAffects(aVRayGeomInfo, aVRayGeomResult);
   attributeAffects(aFilePath, aVRayGeomResult);
   attributeAffects(aObjectExpression, aVRayGeomResult);
   attributeAffects(aStartFrame, aVRayGeomResult);
   attributeAffects(aEndFrame, aVRayGeomResult);
   attributeAffects(aSpeed, aVRayGeomResult);
   attributeAffects(aOffset, aVRayGeomResult);
   attributeAffects(aPreserveStartFrame, aVRayGeomResult);
   attributeAffects(aCycleType, aVRayGeomResult);
#endif

   attributeAffects(aFilePath, aNumShapes);
   attributeAffects(aObjectExpression, aNumShapes);
   attributeAffects(aIgnoreInstances, aNumShapes);
   attributeAffects(aIgnoreVisibility, aNumShapes);
   
   attributeAffects(aFilePath, aAnimated);
   attributeAffects(aObjectExpression, aAnimated);
   attributeAffects(aIgnoreInstances, aAnimated);
   attributeAffects(aIgnoreXforms, aAnimated);
   attributeAffects(aIgnoreVisibility, aAnimated);
   
   attributeAffects(aObjectExpression, aOutBoxMin);
   attributeAffects(aDisplayMode, aOutBoxMin);
   attributeAffects(aCycleType, aOutBoxMin);
   attributeAffects(aTime, aOutBoxMin);
   attributeAffects(aSpeed, aOutBoxMin);
   attributeAffects(aOffset, aOutBoxMin);
   attributeAffects(aPreserveStartFrame, aOutBoxMin);
   attributeAffects(aStartFrame, aOutBoxMin);
   attributeAffects(aEndFrame, aOutBoxMin);
   attributeAffects(aIgnoreXforms, aOutBoxMin);
   attributeAffects(aIgnoreInstances, aOutBoxMin);
   attributeAffects(aIgnoreVisibility, aOutBoxMin);
   
   attributeAffects(aFilePath, aOutBoxMax);
   attributeAffects(aObjectExpression, aOutBoxMax);
   attributeAffects(aDisplayMode, aOutBoxMax);
   attributeAffects(aCycleType, aOutBoxMax);
   attributeAffects(aTime, aOutBoxMax);
   attributeAffects(aSpeed, aOutBoxMax);
   attributeAffects(aOffset, aOutBoxMax);
   attributeAffects(aPreserveStartFrame, aOutBoxMax);
   attributeAffects(aStartFrame, aOutBoxMax);
   attributeAffects(aEndFrame, aOutBoxMax);
   attributeAffects(aIgnoreXforms, aOutBoxMax);
   attributeAffects(aIgnoreInstances, aOutBoxMax);
   attributeAffects(aIgnoreVisibility, aOutBoxMax);
   
   return MS::kSuccess;
}

AbcShape::AbcShape()
   : MPxSurfaceShape()
   , mOffset(0.0)
   , mSpeed(1.0)
   , mCycleType(CT_hold)
   , mStartFrame(0.0)
   , mEndFrame(0.0)
   , mSampleTime(0.0)
   , mIgnoreInstances(false)
   , mIgnoreTransforms(false)
   , mIgnoreVisibility(false)
   , mScene(0)
   , mDisplayMode(DM_box)
   , mNumShapes(0)
   , mPointWidth(0.0f)
   , mLineWidth(0.0f)
   , mPreserveStartFrame(false)
   , mDrawTransformBounds(false)
   , mDrawLocators(false)
   , mUpdateLevel(AbcShape::UL_none)
#ifdef ABCSHAPE_VRAY_SUPPORT
   , mVRFileName("file", "")
   , mVRObjectPath("object_path", "")
   , mVRAnimSpeed("anim_speed", 1.0)
   , mVRAnimType("anim_type", 1)
   , mVRAnimOffset("anim_offset", 0.0)
   , mVRAnimOverride("anim_override", 0)
   , mVRAnimStart("anim_start", 0)
   , mVRAnimLength("anim_length", 0)
   , mVRPrimaryVisibility("primary_visibility", 1)
   , mVRUseAlembicOffset("use_alembic_offset", 0)
   , mVRUseFaceSets("use_face_sets", 0)
   , mVRUseFullNames("use_full_names", 0)
   , mVRComputeBBox("compute_bbox", 0)
   , mVRSmoothUV("smooth_uv", true)
   , mVRMesh("mesh", 0)
   , mVRPreserveMapBorders("preserve_map_borders", -1)
   , mVRStaticSubdiv("static_subdiv", false)
   , mVRClassicCatmark("classic_catmark", false)
   , mVRUseGlobals("use_globals", true)
   , mVRViewDep("view_dep", true)
   , mVREdgeLength("edge_length", 4.0f)
   , mVRMaxSubdivs("max_subdivs", 256)
   , mVRUseBounds("use_bounds", false)
   , mVRMinBound("min_bound", VR::Color(0, 0, 0))
   , mVRMaxBound("max_bound", VR::Color(1, 1, 1))
   , mVRCacheNormals("cache_normals", false)
   , mVRStaticDisp("static_displacement", false)
   , mVRPrecision("precision", 8)
   , mVRDisp2D("displace_2d", false)
   , mVRTightBounds("tight_bounds", false)
   , mVRResolution("resolution", 256)
   , mVRFilterTexture("filter_texture", false)
   , mVRFilterBlur("filter_blur", 0.001f)
   , mVRVectorDisp("vector_displacement", 0)
   , mVRKeepContinuity("keep_continuity", false)
   , mVRWaterLevel("water_level", -1e+30f)
   , mVRDispAmount("displacement_amount", 1.0f)
   , mVRDispShift("displacement_shift", 0.0f)
#endif
{
}

AbcShape::~AbcShape()
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Destructor called" << std::endl;
   #endif
   
   if (mScene && !AlembicSceneCache::Unref(mScene))
   {
      #ifdef _DEBUG
      std::cout << "[" << PREFIX_NAME("AbcShape") << "] Force delete scene" << std::endl;
      #endif
      
      delete mScene;
   }
}

void AbcShape::postConstructor()
{
   setRenderable(true);
}

bool AbcShape::ignoreCulling() const
{
   MFnDependencyNode node(thisMObject());
   MPlug plug = node.findPlug("ignoreCulling");
   if (!plug.isNull())
   {
      return plug.asBool();
   }
   else
   {
      return false;
   }
}

MStatus AbcShape::compute(const MPlug &plug, MDataBlock &block)
{
   if (plug.attribute() == aOutBoxMin || plug.attribute() == aOutBoxMax)
   {
      syncInternals(block);
      
      Alembic::Abc::V3d out(0, 0, 0);
      
      if (mScene)
      {
         if (plug.attribute() == aOutBoxMin)
         {
            out = mScene->selfBounds().min;
         }
         else
         {
            out = mScene->selfBounds().max;
         }
      }
      
      MDataHandle hOut = block.outputValue(plug.attribute());
      
      hOut.set3Double(out.x, out.y, out.z);
      
      block.setClean(plug);
      
      return MS::kSuccess;
   }
   else if (plug.attribute() == aNumShapes)
   {
      syncInternals(block);
      
      MDataHandle hOut = block.outputValue(plug.attribute());
      
      hOut.setInt(mNumShapes);
      
      block.setClean(plug);
      
      return MS::kSuccess;
   }
   else if (plug.attribute() == aAnimated)
   {
      syncInternals(block);
      
      MDataHandle hOut = block.outputValue(plug.attribute());
      
      hOut.setBool(mAnimated);
      
      block.setClean(plug);
      
      return MS::kSuccess;
   }
#ifdef ABCSHAPE_VRAY_SUPPORT
   else if (plug.attribute() == aVRayGeomResult)
   {
      syncInternals(block);
      
      MDataHandle hIn = block.inputValue(aVRayGeomInfo);
      MDataHandle hOut = block.outputValue(plug.attribute());
      
      void *ptr = hIn.asAddr();
      
      VR::VRayGeomInfo *geomInfo = reinterpret_cast<VR::VRayGeomInfo*>(ptr);
      
      if (geomInfo)
      {
         PluginManager *plugman = geomInfo->getPluginManager();
         
         if (plugman)
         {
            PluginDesc *plugdesc = plugman->getPluginDesc("GeomMeshFile");
            
            if (plugdesc)
            {
               /*
               GeomStaticMesh
                 No Subdiv && No Disp

               GeomStaticMesh + GeomStaticSmoothedMesh
                 Subdiv && No Disp 2D

               GeomStaticMesh + GeomDisplacedMesh
                 No Subdiv && Any Disp
                 Subdiv && Disp 2D
                 

               Note: No displacement shader assigned qualifies as "No Disp" (TODO)
               */
     
               bool existing = false;
               
               MObject self = thisMObject();
               MFnDependencyNode thisNode(self);
               
               MDagPath thisPath;
               MDagPath::getAPathTo(self, thisPath);
               
               #ifdef _DEBUG
               std::cout << "Export to V-Ray: \"" << thisNode.name().asChar() << "\" - \"" << thisPath.fullPathName().asChar() << "\"" << std::endl;
               #endif
               
               // Check for assigned displacement shader
               
               MPlug pDispSource;
               MyMFnSetSet dispSets;
               MyMFnSetSet::iterator dispSetIt, assignedDisp;
               
               MItDependencyNodes nodeIt(MFn::kSet);
               
               while (!nodeIt.isDone())
               {
                  MObject obj = nodeIt.thisNode();
                  
                  MyMFnSet set(obj);
                  
                  if (set.typeName() == "VRayDisplacement")
                  {
                     #ifdef _DEBUG
                     std::cout << "Found VRayDisplacement: \"" << set.name().asChar() << "\"" << std::endl;
                     #endif
                     
                     dispSets.insert(set);
                  }
                  
                  nodeIt.next();
               }
               
               assignedDisp = dispSets.end();
               
               while (thisPath.length() > 0)
               {
                  #ifdef _DEBUG
                  std::cout << "Check displacement for \"" << thisPath.fullPathName().asChar() << "\"" << std::endl;
                  #endif
                  
                  for (dispSetIt = dispSets.begin(); dispSetIt != dispSets.end(); ++dispSetIt)
                  {
                     if (dispSetIt->isMember(thisPath))
                     {
                        #ifdef _DEBUG
                        std::cout << "Found displacement: " << dispSetIt->name().asChar() << std::endl;
                        #endif
                        
                        // check for a connection on 
                        MPlug pDisp = dispSetIt->findPlug("displacement");
                        
                        if (!pDisp.isNull())
                        {
                           MPlugArray srcs;
                           
                           pDisp.connectedTo(srcs, true, false);
                           
                           if (srcs.length() > 0)
                           {
                              pDispSource = srcs[0];
                              assignedDisp = dispSetIt;
                           }
                        }
                        
                        break;
                     }
                  }
                  
                  if (assignedDisp != dispSets.end())
                  {
                     break;
                  }
                  
                  thisPath.pop();
               }
               
               // Check subdivision/displacement settings
               MPlug pSubdivEnable = thisNode.findPlug("vraySubdivEnable");
               MPlug pNoDisp = thisNode.findPlug("vrayDisplacementNone");
               MPlug pDispType = thisNode.findPlug("vrayDisplacementType");
               
               bool subdiv = (!pSubdivEnable.isNull() && pSubdivEnable.asBool());
               bool disp = ((assignedDisp != dispSets.end()) && (pNoDisp.isNull() || !pNoDisp.asBool()));
               // should set disp to false if shape has no displacement shader assigned
               bool disp2d = (disp && !pDispType.isNull() && pDispType.asInt() == 0);
               
               #ifdef _DEBUG
               std::cout << "Export \"" << thisNode.name().asChar() << "\" to vray" << std::endl;
               #endif
               
               VR::VRayPlugin *abc = geomInfo->newPlugin("GeomMeshFile", existing);
               VR::VRayPlugin *mod = 0;
               
               #ifdef _DEBUG
               std::cout << "  " << std::hex << (void*)abc << std::dec << (existing ? " (existing)" : "") << std::endl;
               if (abc)
               {
                  const tchar *name = abc->getPluginName();
                  std::cout << "  name = \"" << (name ? name : "") << "\"" << std::endl;
               }
               #endif
               
               if (abc && !existing)
               {
                  // Note: take care that object expression points to a real node as V-Ray GeomMeshFile doesn't support regular expressions
                  
                  mVRFileName.setString(mFilePath.asChar(), 0, 0.0);
                  mVRObjectPath.setString(mObjectExpression.asChar(), 0, 0.0);
                  mVRAnimSpeed.setFloat(float(mSpeed), 0, 0.0);
                  mVRAnimStart.setInt(int(floor(mStartFrame)), 0, 0.0);
                  mVRAnimLength.setInt(int(floor(mEndFrame - mStartFrame)), 0, 0.0);
                  mVRAnimOverride.setBool(1, 0, 0.0);
                  mVRUseFaceSets.setBool(0, 0, 0.0);
                  mVRUseFullNames.setBool(0, 0, 0.0);
                  mVRPrimaryVisibility.setBool(1, 0, 0.0); // need to check this one
                  mVRUseAlembicOffset.setBool(0, 0, 0.0);
                  mVRComputeBBox.setBool(0, 0, 0.0);
                  
                  // If 'time' is not connected, set anim_type to 'still' and set anim_offset accordingly
                  switch (mCycleType)
                  {
                  case CT_bounce:
                     mVRAnimType.setInt(2, 0, 0.0); // 'ping-pong'
                     break;
                  case CT_loop:
                     mVRAnimType.setInt(0, 0, 0.0); // 'loop'
                     break;
                  case CT_reverse:
                     MGlobal::displayWarning(MString("[") + PREFIX_NAME("AbcShape] 'reverse' cycle type not supported by V-Ray GeomMeshFile. Default to 'hold'"));
                  case CT_hold: // 'once'
                  default:
                     mVRAnimType.setInt(1, 0, 0.0);
                  }
                  
                  if (mPreserveStartFrame && fabs(mSpeed) > 0.0001)
                  {
                     mVRAnimOffset.setFloat(-mSpeed * mStartFrame, 0, 0.0);
                  }
                  else
                  {
                     mVRAnimOffset.setFloat(-mStartFrame, 0, 0.0);
                  }
                  
                  abc->setParameter(&mVRFileName);
                  abc->setParameter(&mVRObjectPath);
                  abc->setParameter(&mVRAnimType);
                  abc->setParameter(&mVRAnimOffset);
                  abc->setParameter(&mVRAnimSpeed);
                  abc->setParameter(&mVRAnimOverride);
                  abc->setParameter(&mVRAnimStart);
                  abc->setParameter(&mVRAnimLength);
                  abc->setParameter(&mVRUseAlembicOffset);
                  abc->setParameter(&mVRPrimaryVisibility);
                  abc->setParameter(&mVRUseFullNames);
                  abc->setParameter(&mVRUseFaceSets);
                  abc->setParameter(&mVRComputeBBox);
                  
                  if (subdiv)
                  {
                     MPlug pSubdivUVs = thisNode.findPlug("vraySubdivUVs");
                     if (!pSubdivUVs.isNull())
                     {
                        mVRSmoothUV.setBool(pSubdivUVs.asBool(), 0, 0.0);
                        abc->setParameter(&mVRSmoothUV);
                     }
                  }
                  
                  if (disp2d || (disp && !subdiv))
                  {
                     #ifdef _DEBUG
                     std::cout << "  Create additional GeomDisplacedMesh" << std::endl;
                     #endif
                     
                     plugdesc = plugman->getPluginDesc("GeomDisplacedMesh");
                     
                     if (plugdesc)
                     {
                        existing = false;
                        
                        std::string newName = abc->getPluginName();
                        newName += "@displaced";
                        
                        geomInfo->clearLastPlugin(newName.c_str(), true);
                        
                        mod = geomInfo->newPlugin("GeomDisplacedMesh", existing);
                        
                        #ifdef _DEBUG
                        std::cout << "    " << std::hex << (void*)mod << std::dec << (existing ? " (existing)" : "") << std::endl;
                        if (mod)
                        {
                           const tchar *name = mod->getPluginName();
                           std::cout << "    name = \"" << (name ? name : "") << "\"" << std::endl;
                        }
                        #endif
                        
                        if (mod && !existing)
                        {
                           // Process parameters specific to GeomDisplacedMesh here
                           
                           MPlug pStaticDisp = thisNode.findPlug("vrayDisplacementStatic");
                           if (!pStaticDisp.isNull())
                           {
                              mVRStaticDisp.setBool(pStaticDisp.asBool(), 0, 0.0);
                              mod->setParameter(&mVRStaticDisp);
                           }
                           
                           MPlug pPrecision = thisNode.findPlug("vray2dDisplacementPrecision");
                           if (!pPrecision.isNull())
                           {
                              mVRPrecision.setBool(pPrecision.asInt(), 0, 0.0);
                              mod->setParameter(&mVRPrecision);
                           }
                           
                           if (disp2d)
                           {
                              mVRDisp2D.setBool(true, 0, 0.0);
                              mod->setParameter(&mVRDisp2D);
                              
                              MPlug pTightBounds = thisNode.findPlug("vray2dDisplacementTightBounds");
                              if (!pTightBounds.isNull())
                              {
                                 mVRTightBounds.setBool(pTightBounds.asBool(), 0, 0.0);
                                 mod->setParameter(&mVRTightBounds);
                              }
                              
                              MPlug pResolution = thisNode.findPlug("vray2dDisplacementResolution");
                              if (!pResolution.isNull())
                              {
                                 mVRResolution.setBool(pResolution.asInt(), 0, 0.0);
                                 mod->setParameter(&mVRResolution);
                              }
                              
                              MPlug pFilterTexture = thisNode.findPlug("vray2dDisplacementFilterTexture");
                              if (!pFilterTexture.isNull())
                              {
                                 mVRFilterTexture.setBool(pFilterTexture.asBool(), 0, 0.0);
                                 mod->setParameter(&mVRFilterTexture);
                              }
                              
                              MPlug pFilterBlur = thisNode.findPlug("vray2dDisplacementFilterBlur");
                              if (!pFilterBlur.isNull())
                              {
                                 mVRFilterBlur.setFloat(pFilterBlur.asFloat(), 0, 0.0);
                                 mod->setParameter(&mVRFilterBlur);
                              }
                           }
                        }
                        else
                        {
                           #ifdef _DEBUG
                           if (existing)
                           {
                              std::cout << "    Already exists" << std::endl;
                           }
                           else
                           {
                              std::cout << "    Failed to create" << std::endl;
                           }
                           #endif
                           mod = 0;
                        }
                     }
                     else
                     {
                        #ifdef _DEBUG
                        std::cout << "    No \"GeomDisplacedMesh\" V-Ray plugin" << std::endl;
                        #endif
                     }
                  }
                  else if (subdiv)
                  {
                     #ifdef _DEBUG
                     std::cout << "  Create additional GeomStaticSmoothedMesh" << std::endl;
                     #endif
                     
                     plugdesc = plugman->getPluginDesc("GeomStaticSmoothedMesh");
                     
                     if (plugdesc)
                     {
                        existing = false;
                        
                        std::string newName = abc->getPluginName();
                        newName += "@subdivGeometry";
                        
                        geomInfo->clearLastPlugin(newName.c_str(), true);
                        
                        mod = geomInfo->newPlugin("GeomStaticSmoothedMesh", existing);
                        
                        #ifdef _DEBUG
                        std::cout << "    " << std::hex << (void*)mod << std::dec << (existing ? " (existing)" : "") << std::endl;
                        if (mod)
                        {
                           const tchar *name = mod->getPluginName();
                           std::cout << "    name = \"" << (name ? name : "") << "\"" << std::endl;
                        }
                        #endif
                        
                        if (mod && !existing)
                        {
                           // Process parameters specific to GeomStaticSmoothMesh
                           MPlug pPreserveMapBorder = thisNode.findPlug("vrayPreserveMapBorders");
                           if (!pPreserveMapBorder.isNull())
                           {
                              mVRPreserveMapBorders.setInt(pPreserveMapBorder.asInt(), 0, 0.0);
                              mod->setParameter(&mVRPreserveMapBorders);
                           }
                           
                           MPlug pStaticSubdiv = thisNode.findPlug("vrayStaticSubdiv");
                           if (!pStaticSubdiv.isNull())
                           {
                              mVRStaticSubdiv.setBool(pStaticSubdiv.asBool(), 0, 0.0);
                              mod->setParameter(&mVRStaticSubdiv);
                           }
                           
                           MPlug pClassicCatmark = thisNode.findPlug("vrayClassicalCatmark");
                           if (!pClassicCatmark.isNull())
                           {
                              mVRClassicCatmark.setBool(pClassicCatmark.asBool(), 0, 0.0);
                              mod->setParameter(&mVRClassicCatmark);
                           }
                        }
                        else
                        {
                           #ifdef _DEBUG
                           if (existing)
                           {
                              std::cout << "    Already exists" << std::endl;
                           }
                           else
                           {
                              std::cout << "    Failed to create" << std::endl;
                           }
                           #endif
                           mod = 0;
                        }
                     }
                     else
                     {
                        #ifdef _DEBUG
                        std::cout << "    No 'GeomStaticSmoothedMesh' V-Ray plugin" << std::endl;
                        #endif
                     }
                  }
                  
                  if (mod)
                  {
                     if (subdiv)
                     {
                        MPlug pSubdivOverride = thisNode.findPlug("vrayOverrideGlobalSubQual");
                        if (!pSubdivOverride.isNull())
                        {
                           mVRUseGlobals.setBool(!pSubdivOverride.asBool(), 0, 0.0);
                           mod->setParameter(&mVRUseGlobals);
                           
                           if (pSubdivOverride.asBool())
                           {
                              MPlug pViewDep = thisNode.findPlug("vrayViewDep");
                              if (!pViewDep.isNull())
                              {
                                 mVRViewDep.setBool(pViewDep.asBool(), 0, 0.0);
                                 mod->setParameter(&mVRViewDep);
                              }
                              
                              MPlug pEdgeLength = thisNode.findPlug("vrayEdgeLength");
                              if (!pEdgeLength.isNull())
                              {
                                 mVREdgeLength.setFloat(pEdgeLength.asFloat(), 0, 0.0);
                                 mod->setParameter(&mVREdgeLength);
                              }
                              
                              MPlug pMaxSubdivs = thisNode.findPlug("vrayMaxSubdivs");
                              if (!pMaxSubdivs.isNull())
                              {
                                 mVRMaxSubdivs.setFloat(pMaxSubdivs.asInt(), 0, 0.0);
                                 mod->setParameter(&mVRMaxSubdivs);
                              }
                           }
                        }
                     }
                     
                     if (disp)
                     {
                        bool colorDisp = false;
                        
                        if (pDispType.asInt() >= 2)
                        {
                           // Vector displacement
                           
                           mVRVectorDisp.setInt(pDispType.asInt() - 1, 0, 0.0);
                           mod->setParameter(&mVRVectorDisp);
                           
                           // if (pDispType.asInt() == 4)
                           // {
                           //    mVRObjectSpaceDisp.setBool(true, 0, 0.0);
                           //    mod->setParameter(&mVRObjectSpaceDisp);
                           // }
                           
                           colorDisp = true;
                        }
                        
                        MPlug pUseBounds = thisNode.findPlug("vrayDisplacementUseBounds");
                        if (!pUseBounds.isNull())
                        {
                           mVRUseBounds.setBool(pUseBounds.asBool(), 0, 0.0);
                           mod->setParameter(&mVRUseBounds);
                           
                           if (pUseBounds.asBool())
                           {
                              float x, y, z;
                              MObject dataObj;
                              MStatus stat;
                              
                              MPlug pMinBound = thisNode.findPlug("vrayDisplacementMinValue");
                              if (!pMinBound.isNull())
                              {
                                 dataObj = pMinBound.asMObject();
                                 MFnNumericData data(dataObj, &stat);
                                 
                                 if (stat == MS::kSuccess && data.getData(x, y, z) == MS::kSuccess)
                                 {
                                    mVRMinBound.setColor(VR::Color(x, y, z), 0, 0.0);
                                    mod->setParameter(&mVRMinBound);
                                 }
                              }
                              
                              MPlug pMaxBound = thisNode.findPlug("vrayDisplacementMaxValue");
                              if (!pMaxBound.isNull())
                              {
                                 dataObj = pMaxBound.asMObject();
                                 MFnNumericData data(dataObj, &stat);
                                 
                                 if (stat == MS::kSuccess && data.getData(x, y, z) == MS::kSuccess)
                                 {
                                    mVRMaxBound.setColor(VR::Color(x, y, z), 0, 0.0);
                                    mod->setParameter(&mVRMaxBound);
                                 }
                              }
                           }
                        }
                        
                        MPlug pCacheNormals = thisNode.findPlug("vrayDisplacementCacheNormals");
                        if (!pCacheNormals.isNull())
                        {
                           mVRCacheNormals.setBool(pCacheNormals.asBool(), 0, 0.0);
                           mod->setParameter(&mVRCacheNormals);
                        }
                        
                        MPlug pKeepContinuity = thisNode.findPlug("vrayDisplacementKeepContinuity");
                        if (!pKeepContinuity.isNull())
                        {
                           mVRKeepContinuity.setBool(pKeepContinuity.asBool(), 0, 0.0);
                           mod->setParameter(&mVRKeepContinuity);
                        }
                        
                        MPlug pEnableWaterLevel = thisNode.findPlug("vrayEnableWaterLevel");
                        if (!pEnableWaterLevel.isNull() && pEnableWaterLevel.asBool())
                        {
                           MPlug pWaterLevel = thisNode.findPlug("vrayWaterLevel");
                           if (!pWaterLevel.isNull())
                           {
                              mVRWaterLevel.setFloat(pWaterLevel.asFloat(), 0, 0.0);
                              mod->setParameter(&mVRWaterLevel);
                           }
                        }
                        
                        MPlug pDispAmount = thisNode.findPlug("vrayDisplacementAmount");
                        if (!pDispAmount.isNull())
                        {
                           mVRDispAmount.setFloat(pDispAmount.asFloat(), 0, 0.0);
                           mod->setParameter(&mVRDispAmount);
                        }
                        
                        MPlug pDispShift = thisNode.findPlug("vrayDisplacementShift");
                        if (!pDispShift.isNull())
                        {
                           mVRDispShift.setFloat(pDispShift.asFloat(), 0, 0.0);
                           mod->setParameter(&mVRDispShift);
                        }
                        
                        // Export disp shader
                        MObject dispObj = pDispSource.node();
                        MFnDependencyNode dispTex(dispObj);
                        
                        AbcShapeVRayDisp::DispShapes &dispShapes = AbcShapeVRayDisp::DispTexs[dispTex.name().asChar()];
                        
                        if (colorDisp)
                        {
                           dispShapes.asColor.insert(mod->getPluginName());
                        }
                        else
                        {
                           dispShapes.asFloat.insert(mod->getPluginName());
                        }
                     }
                     
                     mVRMesh.setUserObject(abc, 0, 0.0);
                     mod->setParameter(&mVRMesh);
                  }
                  
                  hOut.setInt(1);
               }
               else
               {
                  hOut.setInt(0);
               }
            }
            else
            {
               hOut.setInt(0);
            }
         }
         else
         {
            hOut.setInt(0);
         }
      }
      else
      {
         hOut.setInt(0);
      }
      
      block.setClean(plug);
      
      return MS::kSuccess;
   }
#endif
   else
   {
      return MS::kUnknownParameter;
   }
}

bool AbcShape::isBounded() const
{
   return true;
}

void AbcShape::syncInternals()
{
   MDataBlock block = forceCache();
   syncInternals(block);
}

void AbcShape::syncInternals(MDataBlock &block)
{
   block.inputValue(aFilePath).asString();
   block.inputValue(aObjectExpression).asString();
   block.inputValue(aTime).asTime();
   block.inputValue(aSpeed).asDouble();
   block.inputValue(aOffset).asDouble();
   block.inputValue(aStartFrame).asDouble();
   block.inputValue(aEndFrame).asDouble();
   block.inputValue(aCycleType).asShort();
   block.inputValue(aIgnoreXforms).asBool();
   block.inputValue(aIgnoreInstances).asBool();
   block.inputValue(aIgnoreVisibility).asBool();
   block.inputValue(aDisplayMode).asShort();
   block.inputValue(aPreserveStartFrame).asBool();
   
   switch (mUpdateLevel)
   {
   case UL_objects:
      updateObjects();
      break;
   case UL_range:
      if (mScene) updateRange();
      break;
   case UL_world:
      if (mScene) updateWorld();
      break;
   case UL_geometry:
      if (mScene) updateGeometry();
      break;
   default:
      break;
   }
   
   mUpdateLevel = UL_none;
}

MBoundingBox AbcShape::boundingBox() const
{
   MBoundingBox bbox;
   
   AbcShape *this2 = const_cast<AbcShape*>(this);
      
   this2->syncInternals();
   
   if (mScene)
   {
      // Use self bounds here as those are taking ignore transform/instance flag into account
      Alembic::Abc::Box3d bounds = mScene->selfBounds();
      
      if (!bounds.isEmpty() && !bounds.isInfinite())
      {
         bbox.expand(MPoint(bounds.min.x, bounds.min.y, bounds.min.z));
         bbox.expand(MPoint(bounds.max.x, bounds.max.y, bounds.max.z));
      }
   }
   
   return bbox;
}

double AbcShape::getFPS() const
{
   float fps = 24.0f;
   MTime::Unit unit = MTime::uiUnit();
   
   if (unit!=MTime::kInvalid)
   {
      MTime time(1.0, MTime::kSeconds);
      fps = static_cast<float>(time.as(unit));
   }

   if (fps <= 0.f )
   {
      fps = 24.0f;
   }

   return fps;
}

double AbcShape::computeAdjustedTime(const double inputTime, const double speed, const double timeOffset) const
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Adjust time: " << (inputTime * getFPS()) << " -> " << ((inputTime - timeOffset) * speed * getFPS()) << std::endl;
   #endif
   
   return (inputTime - timeOffset) * speed;
}

double AbcShape::computeRetime(const double inputTime,
                               const double firstTime,
                               const double lastTime,
                               AbcShape::CycleType cycleType) const
{
   const double playTime = lastTime - firstTime;
   static const double eps = 0.001;
   double retime = inputTime;

   switch (cycleType)
   {
   case CT_loop:
      if (inputTime < (firstTime - eps) || inputTime > (lastTime + eps))
      {
         const double timeOffset = inputTime - firstTime;
         const double playOffset = floor(timeOffset/playTime);
         const double fraction = fabs(timeOffset/playTime - playOffset);

         retime = firstTime + playTime * fraction;
      }
      break;
   case CT_reverse:
      if (inputTime > (firstTime + eps) && inputTime < (lastTime - eps))
      {
         const double timeOffset = inputTime - firstTime;
         const double playOffset = floor(timeOffset/playTime);
         const double fraction = fabs(timeOffset/playTime - playOffset);

         retime = lastTime - playTime * fraction;
      }
      else if (inputTime < (firstTime + eps))
      {
         retime = lastTime;
      }
      else
      {
         retime = firstTime;
      }
      break;
   case CT_bounce:
      if (inputTime < (firstTime - eps) || inputTime > (lastTime + eps))
      {
         const double timeOffset = inputTime - firstTime;
         const double playOffset = floor(timeOffset/playTime);
         const double fraction = fabs(timeOffset/playTime - playOffset);

         // forward loop
         if (int(playOffset) % 2 == 0)
         {
            retime = firstTime + playTime * fraction;
         }
         // backward loop
         else
         {
            retime = lastTime - playTime * fraction;
         }
      }
      break;
   case CT_hold:
   default:
      if (inputTime < (firstTime - eps))
      {
         retime = firstTime;
      }
      else if (inputTime > (lastTime + eps))
      {
         retime = lastTime;
      }
      break;
   }

   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Retime: " << (inputTime * getFPS()) << " -> " << (retime * getFPS()) << std::endl;
   #endif
   
   return retime;
}

double AbcShape::getSampleTime() const
{
   double invFPS = 1.0 / getFPS();
   double startOffset = 0.0f;
   if (mPreserveStartFrame && fabs(mSpeed) > 0.0001)
   {
      startOffset = (mStartFrame * (mSpeed - 1.0) / mSpeed);
   }
   double sampleTime = computeAdjustedTime(mTime.as(MTime::kSeconds), mSpeed, (startOffset + mOffset) * invFPS);
   return computeRetime(sampleTime, mStartFrame * invFPS, mEndFrame * invFPS, mCycleType);
}

void AbcShape::updateObjects()
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Update objects: \"" << mFilePath.asChar() << "\" | \"" << mObjectExpression.asChar() << "\"" << std::endl;
   #endif
   
   mGeometry.clear();
   mAnimated = false;
   mNumShapes = 0;
   
   mSceneFilter.set(mObjectExpression.asChar(), "");
   
   AlembicScene *scene = AlembicSceneCache::Ref(mFilePath.asChar(), mSceneFilter);
   
   if (mScene && !AlembicSceneCache::Unref(mScene))
   {
      delete mScene;
   }
   
   mScene = scene;
   
   if (mScene)
   {
      updateRange();
   }
}

void AbcShape::updateRange()
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Update range" << std::endl;
   #endif
   
   mAnimated = false;
   
   GetFrameRange visitor(mIgnoreTransforms, mIgnoreInstances, mIgnoreVisibility);
   mScene->visit(AlembicNode::VisitDepthFirst, visitor);
   
   double start, end;
   
   if (visitor.getFrameRange(start, end))
   {
      double fps = getFPS();
      start *= fps;
      end *= fps;
      
      mAnimated = (fabs(end - start) > 0.0001);
      
      if (fabs(mStartFrame - start) > 0.0001 ||
          fabs(mEndFrame - end) > 0.0001)
      {
         #ifdef _DEBUG
         std::cout << "[" << PREFIX_NAME("AbcShape") << "] Frame range: " << start << " - " << end << std::endl;
         #endif
         
         mStartFrame = start;
         mEndFrame = end;
         mSampleTime = getSampleTime();
         
         // This will force instant refresh of AE values
         // but won't trigger any update as mStartFrame and mEndFrame are unchanged
         MPlug plug(thisMObject(), aStartFrame);
         plug.setDouble(mStartFrame);
         
         plug.setAttribute(aEndFrame);
         plug.setDouble(mEndFrame);
      }
   }
   
   updateWorld();
}

void AbcShape::updateWorld()
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Update world" << std::endl;
   #endif
   
   WorldUpdate visitor(mSampleTime, mIgnoreTransforms, mIgnoreInstances, mIgnoreVisibility);
   mScene->visit(AlembicNode::VisitDepthFirst, visitor);
   
   // only get number of visible shapes
   mNumShapes = visitor.numShapes(true);
   
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] " << mNumShapes << " shape(s) in scene" << std::endl;
   #endif
   
   mScene->updateChildBounds();
   mScene->setSelfBounds(visitor.bounds());
   
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Scene bounds: " << visitor.bounds().min << " - " << visitor.bounds().max << std::endl;
   #endif
   
   if (mDisplayMode >= DM_points)
   {
      updateGeometry();
   }
}

void AbcShape::updateGeometry()
{
   #ifdef _DEBUG
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Update geometry" << std::endl;
   #endif
   
   SampleGeometry visitor(mSampleTime, &mGeometry);
   mScene->visit(AlembicNode::VisitDepthFirst, visitor);
}

void AbcShape::printInfo(bool detailed) const
{
   if (detailed)
   {
      PrintInfo pinf(mIgnoreTransforms, mIgnoreInstances, mIgnoreVisibility);
      mScene->visit(AlembicNode::VisitDepthFirst, pinf);
   }
   
   printSceneBounds();
}

void AbcShape::printSceneBounds() const
{
   std::cout << "[" << PREFIX_NAME("AbcShape") << "] Scene " << mScene->selfBounds().min << " - " << mScene->selfBounds().max << std::endl;
}

bool AbcShape::getInternalValueInContext(const MPlug &plug, MDataHandle &handle, MDGContext &ctx)
{
   if (plug == aFilePath)
   {
      handle.setString(mFilePath);
      return true;
   }
   else if (plug == aObjectExpression)
   {
      handle.setString(mObjectExpression);
      return true;
   }
   else if (plug == aTime)
   {
      handle.setMTime(mTime);
      return true;
   }
   else if (plug == aOffset)
   {
      handle.setDouble(mOffset);
      return true;
   }
   else if (plug == aSpeed)
   {
      handle.setDouble(mSpeed);
      return true;
   }
   else if (plug == aPreserveStartFrame)
   {
      handle.setBool(mPreserveStartFrame);
      return true;
   }
   else if (plug == aCycleType)
   {
      handle.setInt(mCycleType);
      return true;
   }
   else if (plug == aStartFrame)
   {
      handle.setDouble(mStartFrame);
      return true;
   }
   else if (plug == aEndFrame)
   {
      handle.setDouble(mEndFrame);
      return true;
   }
   else if (plug == aIgnoreXforms)
   {
      handle.setBool(mIgnoreTransforms);
      return true;
   }
   else if (plug == aIgnoreInstances)
   {
      handle.setBool(mIgnoreInstances);
      return true;
   }
   else if (plug == aIgnoreVisibility)
   {
      handle.setBool(mIgnoreVisibility);
      return true;
   }
   else if (plug == aDisplayMode)
   {
      handle.setInt(mDisplayMode);
      return true;
   }
   else if (plug == aLineWidth)
   {
      handle.setFloat(mLineWidth);
      return true;
   }
   else if (plug == aPointWidth)
   {
      handle.setFloat(mPointWidth);
      return true;
   }
   else if (plug == aDrawTransformBounds)
   {
      handle.setBool(mDrawTransformBounds);
      return true;
   }
   else if (plug == aDrawLocators)
   {
      handle.setBool(mDrawLocators);
      return true;
   }
   else
   {
      return MPxNode::getInternalValueInContext(plug, handle, ctx);
   }
}

bool AbcShape::setInternalValueInContext(const MPlug &plug, const MDataHandle &handle, MDGContext &ctx)
{
   bool sampleTimeUpdate = false;
   
   if (plug == aFilePath)
   {
      // Note: path seems to already be directory mapped when we reach here (consequence of setUsedAsFilename(true) when the attribute is created)
      //       don't need to use MFileObject to get the resolved path
      MString filePath = handle.asString();
      
      if (filePath != mFilePath)
      {
         mFilePath = filePath;
         mUpdateLevel = UL_objects;
      }
      
      return true;
   }
   else if (plug == aObjectExpression)
   {
      MString objectExpression = handle.asString();
      
      if (objectExpression != mObjectExpression)
      {
         mObjectExpression = objectExpression;
         mUpdateLevel = UL_objects;
      }
      
      return true;
   }
   else if (plug == aIgnoreXforms)
   {
      bool ignoreTransforms = handle.asBool();
      
      if (ignoreTransforms != mIgnoreTransforms)
      {
         mIgnoreTransforms = ignoreTransforms;
         if (mScene)
         {
            mUpdateLevel = std::max<int>(mUpdateLevel, UL_range);
         }
      }
      
      return true;
   }
   else if (plug == aIgnoreInstances)
   {
      bool ignoreInstances = handle.asBool();
      
      if (ignoreInstances != mIgnoreInstances)
      {
         mIgnoreInstances = ignoreInstances;
         if (mScene)
         {
            mUpdateLevel = std::max<int>(mUpdateLevel, UL_range);
         }
      }
      
      return true;
   }
   else if (plug == aIgnoreVisibility)
   {
      bool ignoreVisibility = handle.asBool();
      
      if (ignoreVisibility != mIgnoreVisibility)
      {
         mIgnoreVisibility = ignoreVisibility;
         if (mScene)
         {
            mUpdateLevel = std::max<int>(mUpdateLevel, UL_range);
         }
      }
      
      return true;
   }
   else if (plug == aDisplayMode)
   {
      DisplayMode dm = (DisplayMode) handle.asShort();
      
      if (dm != mDisplayMode)
      {
         bool updateGeo = (mDisplayMode <= DM_boxes && dm >= DM_points);
         
         mDisplayMode = dm;
         
         if (updateGeo && mScene)
         {
            mUpdateLevel = std::max<int>(mUpdateLevel, UL_geometry);
         }
      }
      
      return true;
   }
   else if (plug == aTime)
   {
      MTime t = handle.asTime();
      
      if (fabs(t.as(MTime::kSeconds) - mTime.as(MTime::kSeconds)) > 0.0001)
      {
         mTime = t;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aSpeed)
   {
      double speed = handle.asDouble();
      
      if (fabs(speed - mSpeed) > 0.0001)
      {
         mSpeed = speed;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aPreserveStartFrame)
   {
      bool psf = handle.asBool();
      
      if (psf != mPreserveStartFrame)
      {
         mPreserveStartFrame = psf;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aOffset)
   {
      double offset = handle.asDouble();
      
      if (fabs(offset - mOffset) > 0.0001)
      {
         mOffset = offset;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aCycleType)
   {
      CycleType c = (CycleType) handle.asShort();
      
      if (c != mCycleType)
      {
         mCycleType = c;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aStartFrame)
   {
      double sf = handle.asDouble();
      
      if (fabs(sf - mStartFrame) > 0.0001)
      {
         mStartFrame = sf;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aEndFrame)
   {
      double ef = handle.asDouble();
      
      if (fabs(ef - mEndFrame) > 0.0001)
      {
         mEndFrame = ef;
         sampleTimeUpdate = mAnimated;
      }
   }
   else if (plug == aLineWidth)
   {
      mLineWidth = handle.asFloat();
      return true;
   }
   else if (plug == aPointWidth)
   {
      mPointWidth = handle.asFloat();
      return true;
   }
   else if (plug == aDrawTransformBounds)
   {
      mDrawTransformBounds = handle.asBool();
      return true;
   }
   else if (plug == aDrawLocators)
   {
      mDrawLocators = handle.asBool();
      return true;
   }
   else
   {
      return MPxNode::setInternalValueInContext(plug, handle, ctx);
   }
   
   if (sampleTimeUpdate)
   {
      double sampleTime = getSampleTime();
      
      if (fabs(mSampleTime - sampleTime) > 0.0001)
      {
         mSampleTime = sampleTime;
         
         if (mScene)
         {
            mUpdateLevel = std::max<int>(mUpdateLevel, UL_world);
         }
      }
   }
   
   return true;
}

void AbcShape::copyInternalData(MPxNode *source)
{
   if (source && source->typeId() == ID)
   {
      AbcShape *node = (AbcShape*)source;
      
      mFilePath = node->mFilePath;
      mObjectExpression = node->mObjectExpression;
      mTime = node->mTime;
      mOffset = node->mOffset;
      mSpeed = node->mSpeed;
      mCycleType = node->mCycleType;
      mStartFrame = node->mStartFrame;
      mEndFrame = node->mEndFrame;
      mSampleTime = node->mSampleTime;
      mIgnoreInstances = node->mIgnoreInstances;
      mIgnoreTransforms = node->mIgnoreTransforms;
      mIgnoreVisibility = node->mIgnoreVisibility;
      mLineWidth = node->mLineWidth;
      mPointWidth = node->mPointWidth;
      mDisplayMode = node->mDisplayMode;
      mPreserveStartFrame = node->mPreserveStartFrame;
      mDrawTransformBounds = node->mDrawTransformBounds;
      mDrawLocators = node->mDrawLocators;
      mAnimated = node->mAnimated;
      
      if (mScene && !AlembicSceneCache::Unref(mScene))
      {
         delete mScene;
      }
      mScene = 0;
      mSceneFilter.reset();
      mNumShapes = 0;
      mGeometry.clear();
      mUpdateLevel = UL_objects;
   }
   
}

// ---

void* AbcShapeUI::creator()
{
   return new AbcShapeUI();
}

AbcShapeUI::AbcShapeUI()
   : MPxSurfaceShapeUI()
{
}

AbcShapeUI::~AbcShapeUI()
{
}

void AbcShapeUI::getDrawRequests(const MDrawInfo &info,
                                 bool /*objectAndActiveOnly*/,
                                 MDrawRequestQueue &queue)
{
   MDrawData data;
   getDrawData(0, data);
   
   M3dView::DisplayStyle appearance = info.displayStyle();

   M3dView::DisplayStatus displayStatus = info.displayStatus();
   
   MDagPath path = info.multiPath();
   
   M3dView view = info.view();
   
   MColor color;
   
   switch (displayStatus)
   {
   case M3dView::kLive:
      color = M3dView::liveColor();
      break;
   case M3dView::kHilite:
      color = M3dView::hiliteColor();
      break;
   case M3dView::kTemplate:
      color = M3dView::templateColor();
      break;
   case M3dView::kActiveTemplate:
      color = M3dView::activeTemplateColor();
      break;
   case M3dView::kLead:
      color = M3dView::leadColor();
      break;
   case M3dView::kActiveAffected:
      color = M3dView::activeAffectedColor();
      break;
   default:
      color = MHWRender::MGeometryUtilities::wireframeColor(path);
   }
   
   switch (appearance)
   {
   case M3dView::kBoundingBox:
      {
         MDrawRequest request = info.getPrototype(*this);
         
         request.setDrawData(data);
         request.setToken(kDrawBox);
         request.setColor(color);
         
         queue.add(request);
      }
      break;
   case M3dView::kPoints:
      {
         MDrawRequest request = info.getPrototype(*this);
         
         request.setDrawData(data);
         request.setToken(kDrawPoints);
         request.setColor(color);
         
         queue.add(request);
      }
      break;
   default:
      {
         bool drawWireframe = false;
         
         if (appearance == M3dView::kWireFrame ||
             view.wireframeOnShaded() ||
             displayStatus == M3dView::kActive ||
             displayStatus == M3dView::kHilite ||
             displayStatus == M3dView::kLead)
         {
            MDrawRequest request = info.getPrototype(*this);
            
            request.setDrawData(data);
            request.setToken(appearance != M3dView::kWireFrame ? kDrawGeometryAndWireframe : kDrawGeometry);
            request.setDisplayStyle(M3dView::kWireFrame);
            request.setColor(color);
            
            queue.add(request);
            
            drawWireframe = true;
         }
         
         if (appearance != M3dView::kWireFrame)
         {
            MDrawRequest request = info.getPrototype(*this);
            
            request.setDrawData(data);
            request.setToken(drawWireframe ? kDrawGeometryAndWireframe : kDrawGeometry);
            
            // Only set material info if necessary
            AbcShape *shape = (AbcShape*) surfaceShape();
            if (shape && shape->displayMode() == AbcShape::DM_geometry)
            {
               MMaterial material = (view.usingDefaultMaterial() ? MMaterial::defaultMaterial() : MPxSurfaceShapeUI::material(path));
               material.evaluateMaterial(view, path);
               //material.evaluateTexture(data);
               //bool isTransparent = false;
               //if (material.getHasTransparency(isTransparent) && isTransparent)
               //{
               //  request.setIsTransparent(true);
               //}
               request.setMaterial(material);
            }
            else
            {
               request.setColor(color);
            }
            
            queue.add(request);
         }
      }
   }
}

void AbcShapeUI::draw(const MDrawRequest &request, M3dView &view) const
{
   AbcShape *shape = (AbcShape*) surfaceShape();
   if (!shape)
   {
      return;
   }
   
   switch (request.token())
   {
   case kDrawBox:
      drawBox(shape, request, view);
      break;
   case kDrawPoints:
      drawPoints(shape, request, view);
      break;
   case kDrawGeometry:
   case kDrawGeometryAndWireframe:
   default:
      {
         switch (shape->displayMode())
         {
         case AbcShape::DM_box:
         case AbcShape::DM_boxes:
            drawBox(shape, request, view);
            break;
         case AbcShape::DM_points:
            drawPoints(shape, request, view);
            break;
         default:
            drawGeometry(shape, request, view);
         }
      }
      break;
   }
}

bool AbcShapeUI::computeFrustum(M3dView &view, Frustum &frustum) const
{
   MMatrix projMatrix, modelViewMatrix;
   
   view.projectionMatrix(projMatrix);
   view.modelViewMatrix(modelViewMatrix);
   
   MMatrix tmp = (modelViewMatrix * projMatrix).inverse();
   
   if (tmp.isSingular())
   {
      return false;
   }
   else
   {
      M44d projViewInv;
      
      tmp.get(projViewInv.x);
      
      frustum.setup(projViewInv);
      
      return true;
   }
}

bool AbcShapeUI::computeFrustum(Frustum &frustum) const
{
   // using GL matrix
   M44d projMatrix;
   M44d modelViewMatrix;
   
   glGetDoublev(GL_PROJECTION_MATRIX, &(projMatrix.x[0][0]));
   glGetDoublev(GL_MODELVIEW_MATRIX, &(modelViewMatrix.x[0][0]));
   
   M44d projViewInv = modelViewMatrix * projMatrix;
   
   try
   {
      projViewInv.invert(true);
      frustum.setup(projViewInv);
      return true;
   }
   catch (std::exception &)
   {
      return false;
   }
}

void AbcShapeUI::getWorldMatrix(M3dView &view, Alembic::Abc::M44d &worldMatrix) const
{
   MMatrix modelViewMatrix;
   
   view.modelViewMatrix(modelViewMatrix);
   modelViewMatrix.get(worldMatrix.x);
}

bool AbcShapeUI::select(MSelectInfo &selectInfo,
                        MSelectionList &selectionList,
                        MPointArray &worldSpaceSelectPts) const
{
   MSelectionMask mask(PREFIX_NAME("AbcShape"));
   if (!selectInfo.selectable(mask))
   {
      return false;
   }
   
   AbcShape *shape = (AbcShape*) surfaceShape();
   if (!shape)
   {
      return false;
   }
   
   M3dView::DisplayStyle style = selectInfo.displayStyle();
   
   DrawToken target = (style == M3dView::kBoundingBox ? kDrawBox : (style == M3dView::kPoints ? kDrawPoints : kDrawGeometry));
   
   M3dView view = selectInfo.view();
   
   Frustum frustum;
   
   // As we use same name for all shapes, without hierarchy, don't really need a big buffer
   GLuint *buffer = new GLuint[16];
   
   view.beginSelect(buffer, 16);
   view.pushName(0);
   view.loadName(1); // Use same name for all 
   
   glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LIGHTING_BIT | GL_LINE_BIT | GL_POINT_BIT);
   glDisable(GL_LIGHTING);
   
   if (shape->displayMode() == AbcShape::DM_box)
   {
      if (target == kDrawPoints)
      {
         DrawBox(shape->scene()->selfBounds(), true, shape->pointWidth());
      }
      else
      {
         DrawBox(shape->scene()->selfBounds(), false, shape->lineWidth());
      }
   }
   else
   {
      DrawScene visitor(shape->sceneGeometry(),
                        shape->ignoreTransforms(),
                        shape->ignoreInstances(),
                        shape->ignoreVisibility());
      
      // Use matrices staight from OpenGL as those will include the picking matrix so
      //   that more geometry can get culled
      if (computeFrustum(frustum))
      {
         #ifdef _DEBUG
         #if 0
         // Let's see the difference
         M44d glProjMatrix;
         M44d glModelViewMatrix;
         MMatrix mayaProjMatrix;
         MMatrix mayaModelViewMatrix;
         
         glGetDoublev(GL_PROJECTION_MATRIX, &(glProjMatrix.x[0][0]));
         glGetDoublev(GL_MODELVIEW_MATRIX, &(glModelViewMatrix.x[0][0]));
         
         M44d glProjView = glModelViewMatrix * glProjMatrix;
         
         view.projectionMatrix(mayaProjMatrix);
         view.modelViewMatrix(mayaModelViewMatrix);
   
         MMatrix tmp = (mayaModelViewMatrix * mayaProjMatrix);
         M44d mayaProjView;
         tmp.get(mayaProjView.x);
         
         std::cout << "Proj/View from GL:" << std::endl;
         std::cout << glProjView << std::endl;
         
         std::cout << "Proj/View from Maya:" << std::endl;
         std::cout << mayaProjView << std::endl;
         #endif
         #endif
         
         visitor.doCull(frustum);
      }
      visitor.setLineWidth(shape->lineWidth());
      visitor.setPointWidth(shape->pointWidth());
      
      if (target == kDrawBox)
      {
         visitor.drawBounds(true);
      }
      else if (target == kDrawPoints)
      {
         visitor.drawBounds(shape->displayMode() == AbcShape::DM_boxes);
         visitor.drawAsPoints(true);
      }
      else
      {
         if (shape->displayMode() == AbcShape::DM_boxes)
         {
            visitor.drawBounds(true);
         }
         else if (shape->displayMode() == AbcShape::DM_points)
         {
            visitor.drawAsPoints(true);
         }
         else if (style == M3dView::kWireFrame)
         {
            visitor.drawWireframe(true);
         }
      }
      
      shape->scene()->visit(AlembicNode::VisitDepthFirst, visitor);
   }
   
   glPopAttrib();
   
   view.popName();
   
   int hitCount = view.endSelect();
   
   if (hitCount > 0)
   {
      unsigned int izdepth = 0xFFFFFFFF;
      GLuint *curHit = buffer;
      
      for (int i=hitCount; i>=0; --i)
      {
         if (curHit[0] && izdepth > curHit[1])
         {
            izdepth = curHit[1];
         }
         curHit += curHit[0] + 3;
      }
      
      MDagPath path = selectInfo.multiPath();
      while (path.pop() == MStatus::kSuccess)
      {
         if (path.hasFn(MFn::kTransform))
         {
            break;
         }
      }
      
      MSelectionList selectionItem;
      selectionItem.add(path);
      
      MPoint worldSpacePoint;
      // compute hit point
      {
         float zdepth = float(izdepth) / 0xFFFFFFFF;
         
         MDagPath cameraPath;
         view.getCamera(cameraPath);
         
         MFnCamera camera(cameraPath);
         
         if (!camera.isOrtho())
         {
            // z is normalized but non linear
            double nearp = camera.nearClippingPlane();
            double farp = camera.farClippingPlane();
            
            zdepth *= (nearp / (farp - zdepth * (farp - nearp)));
         }
         
         MPoint O;
         MVector D;
         
         selectInfo.getLocalRay(O, D);
         O = O * selectInfo.multiPath().inclusiveMatrix();
         
         short x, y;
         view.worldToView(O, x, y);
         
         MPoint Pn, Pf;
         view.viewToWorld(x, y, Pn, Pf);
         
         worldSpacePoint = Pn + zdepth * (Pf - Pn);
      }
      
      selectInfo.addSelection(selectionItem,
                              worldSpacePoint,
                              selectionList,
                              worldSpaceSelectPts,
                              mask,
                              false);
      
      delete[] buffer;
      
      return true;
   }
   else
   {
      return false;
   }
}

void AbcShapeUI::drawBox(AbcShape *shape, const MDrawRequest &, M3dView &view) const
{
   view.beginGL();
   
   glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LIGHTING_BIT | GL_LINE_BIT);
   
   glDisable(GL_LIGHTING);
   
   if (shape->displayMode() == AbcShape::DM_box)
   {
      DrawBox(shape->scene()->selfBounds(), false, shape->lineWidth());
   }
   else
   {
      Frustum frustum;
      
      DrawScene visitor(NULL, shape->ignoreTransforms(), shape->ignoreInstances(), shape->ignoreVisibility());
      visitor.drawAsPoints(false);
      visitor.drawLocators(shape->drawLocators());
      if (!shape->ignoreCulling() && computeFrustum(view, frustum))
      {
         visitor.doCull(frustum);
      }
      if (shape->drawTransformBounds())
      {
         Alembic::Abc::M44d worldMatrix;
         getWorldMatrix(view, worldMatrix);
         visitor.drawTransformBounds(true, worldMatrix);
      }
      
      shape->scene()->visit(AlembicNode::VisitDepthFirst, visitor);
   }
   
   glPopAttrib();
   
   view.endGL();
}

void AbcShapeUI::drawPoints(AbcShape *shape, const MDrawRequest &, M3dView &view) const
{
   view.beginGL();
   
   glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LIGHTING_BIT | GL_POINT_BIT);
   
   glDisable(GL_LIGHTING);
   
   if (shape->displayMode() == AbcShape::DM_box)
   {
      DrawBox(shape->scene()->selfBounds(), true, shape->pointWidth());
   }
   else
   {
      Frustum frustum;
   
      DrawScene visitor(shape->sceneGeometry(), shape->ignoreTransforms(), shape->ignoreInstances(), shape->ignoreVisibility());
      
      visitor.drawBounds(shape->displayMode() == AbcShape::DM_boxes);
      visitor.drawAsPoints(true);
      visitor.setLineWidth(shape->lineWidth());
      visitor.setPointWidth(shape->pointWidth());
      visitor.drawLocators(shape->drawLocators());
      if (!shape->ignoreCulling() && computeFrustum(view, frustum))
      {
         visitor.doCull(frustum);
      }
      if (shape->drawTransformBounds())
      {
         Alembic::Abc::M44d worldMatrix;
         getWorldMatrix(view, worldMatrix);
         visitor.drawTransformBounds(true, worldMatrix);
      }
      
      shape->scene()->visit(AlembicNode::VisitDepthFirst, visitor);
   }
      
   glPopAttrib();
      
   view.endGL();
}

void AbcShapeUI::drawGeometry(AbcShape *shape, const MDrawRequest &request, M3dView &view) const
{
   Frustum frustum;
   
   view.beginGL();
   
   bool wireframeOnShaded = (request.token() == kDrawGeometryAndWireframe);
   bool wireframe = (request.displayStyle() == M3dView::kWireFrame);
   //bool flat = (request.displayStyle() == M3dView::kFlatShaded);
   
   glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LIGHTING_BIT | GL_LINE_BIT | GL_POLYGON_BIT);
   
   if (wireframe)
   {
      glDisable(GL_LIGHTING);
      //if over shaded: glDepthMask(false)?
   }
   else
   {
      glEnable(GL_POLYGON_OFFSET_FILL);
      
      //glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
      // Note: as we only store 1 smooth normal per point in mesh, flat shaded will look strange: ignore it
      glShadeModel(GL_SMOOTH);
      
      //glEnable(GL_CULL_FACE);
      //glCullFace(GL_BACK);
      
      glEnable(GL_COLOR_MATERIAL);
      glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
      
      MMaterial material = request.material();
      material.setMaterial(request.multiPath(), request.isTransparent());
      
      //bool useTextures = (material.materialIsTextured() && !view.useDefaultMaterial());
      //if (useTextures)
      //{
      //  glEnable(GL_TEXTURE_2D);
      //  material.applyTexture(view, data);
      //  // ... and set mesh UVs
      //}
      
      MColor defaultDiffuse;
      material.getDiffuse(defaultDiffuse);
      glColor3f(defaultDiffuse.r, defaultDiffuse.g, defaultDiffuse.b);
   }
   
   DrawScene visitor(shape->sceneGeometry(), shape->ignoreTransforms(), shape->ignoreInstances(), shape->ignoreVisibility());
   visitor.drawWireframe(wireframe);
   visitor.setLineWidth(shape->lineWidth());
   visitor.setPointWidth(shape->pointWidth());
   if (!shape->ignoreCulling() && computeFrustum(view, frustum))
   {
      visitor.doCull(frustum);
   }
   
   // only draw transform bounds and locators once
   if (!wireframeOnShaded || wireframe)
   {
      visitor.drawLocators(shape->drawLocators());
      if (shape->drawTransformBounds())
      {
         Alembic::Abc::M44d worldMatrix;
         getWorldMatrix(view, worldMatrix);
         visitor.drawTransformBounds(true, worldMatrix);
      }
   }
   
   shape->scene()->visit(AlembicNode::VisitDepthFirst, visitor);
   
   glPopAttrib();
   
   view.endGL();
}
