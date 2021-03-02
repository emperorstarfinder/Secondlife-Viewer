#!runpy.sh

"""\

This module contains tools for analyzing arctan format logging sessions.

$LicenseInfo:firstyear=2016&license=viewerlgpl$
Second Life Viewer Source Code
Copyright (C) 2016, Linden Research, Inc.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
$/LicenseInfo$
"""

import argparse
from lxml import etree
import sys
from llbase import llsd
import re
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
import itertools

def xstr(x):
    if x is None:
        return ''
    return str(x)

def get_default_export_name(pd_data,prefix="performance"):
    res = None
    unique_id = pd_data["Session.UniqueHostID"][0]
    session_id = pd_data["Session.UniqueSessionUUID"][0]
    name = prefix + "_" + str(unique_id)[0:6] + "_" + str(session_id) + ".csv"

    res = name.replace(":",".").replace("Z","").replace("T","-")
        #fig.savefig("times_histo_outfit_" + label.replace(" ","_").replace("*","") + ".pdf", bbox_inches="tight")
    print "unique_id",unique_id,"session_id",session_id,"name",res
    return res
    
def export(filename, timers):
    print "export",filename
    if filename=="auto":
        filename = get_default_export_name(timers)
        print "saving to",filename
    if filename:
        if re.match(".*\.csv$", filename):
            timers.to_csv(filename)
            return filename
        else:
            print "unknown extension for export",filename
    return None

class DerivedSelfTimers:
    def __init__(self, sd, **kwargs):
        self.sd = sd
        self.kwargs = kwargs
    def __getitem__(self, key):
        value = self.sd["Timers"][key]["Time"]
        if "children" in self.kwargs:
            children = self.kwargs["children"]
            if children is not None:
                for child in children.get(key,[]):
                    if child in self.sd["Timers"]:
                        if not child in ignore:
                            value -= self.sd["Timers"][child]["Time"]
        return value

# for these, we will compute the fraction of triangles affected by the setting
bool_graphic_properties = ["alpha","animtex","bump","flexi","glow","invisi","particles","planar","produces_light","shiny","weighted_mesh","specmap","normalmap","materials"]

# for these, we will sum all counts found in all attachments
sum_graphic_properties = ["media_faces"]

class AttachmentsDerivedField:
    def __init__(self,sd,**kwargs):
        self.sd = sd
        self.kwargs = kwargs
    def __getitem__(self, key):
        attachments = sd_extract_field(self.sd,"Avatars.Self.Attachments") 
        triangle_keys = ["triangles_high"]
        if attachments:
            if key=="Count":
                return len(attachments)
            if key=="MeshCount":
                return len([att for att in attachments if att["isMesh"]])
            elif key in triangle_keys:
                total = sum([sd_extract_field(att,"StreamingCost." + key,0.0) for att in attachments])
                return total
            elif key in bool_graphic_properties:
                tris_with_property = sum([sd_extract_field(att,"StreamingCost.triangles_high",0.0) for att in attachments if sd_extract_field(att,key)])
                tris_grand_total = sum([sd_extract_field(att,"StreamingCost.triangles_high",0.0) for att in attachments])
                return tris_with_property/tris_grand_total if tris_grand_total > 0.0 else 0.0
            elif key in sum_graphic_properties:
                return sum([sd_extract_field(att,"StreamingCost." + key,0.0) for att in attachments])
            else:
                raise IndexError()

class DerivedAvatarField:
    def __init__(self,sd,**kwargs):
        self.sd = sd
        self.kwargs = kwargs
    def __getitem__(self, key):
        if key=="AttachmentCount":
            attachments = sd_extract_field(self.sd,"Avatars.Self.Attachments") 
            if attachments:
                return len(attachments)
            return None
        if key=="Attachments":
            return AttachmentsDerivedField(self.sd,**self.kwargs)
        else:
            raise IndexError()

class DerivedFieldGetter:
    def __init__(self,sd,**kwargs):
        self.sd = sd
        self.kwargs = kwargs

    def __getitem__(self, key):
        if key=="Timers":
            return DerivedSelfTimers(self.sd, **self.kwargs)
        elif key=="Avatar":
            return DerivedAvatarField(self.sd, **self.kwargs)
        else:
            return DerivedSelfTimers(self.sd, **self.kwargs)

# optimized to avoid blowing out memory when processing huge files,
# but not especially efficient the way we parse twice.
def get_frame_record(filename,**kwargs):
    f = open(filename)

    # get an iterable
    context = etree.iterparse(f, events=("start", "end"))

    # turn it into an iterator
    context = iter(context)

    # get the root element
    event, root = context.next()

    frame_count = 0
    my_total = 0.0
    try:
        for event, elem in context:
            if event == "end" and elem.tag == "llsd":
                # process frame record here
                frame_count += 1

                xmlstr = etree.tostring(elem, encoding="utf8", method="xml")
                sd = llsd.parse_xml(xmlstr)
                sd['Derived'] = DerivedFieldGetter(sd, **kwargs)
                yield sd
    except etree.XMLSyntaxError:
        print "Fell off end of document"

    print "Read",frame_count,"frame records"
    f.close()

def sd_extract_field(sd,key,default_val=None):
    chain = key.split(".")
    t = sd
    try:
        for subkey in chain:
            t = t[subkey]
        return t
    except KeyError:
        return default_val

def fill_blanks(df):
    print "fill_blanks"
    for col in df:
        if col.startswith("Timers."):
            df[col].fillna(0.0, inplace=True)
        else:
            # Intermittently recorded, can just fill in
            df[col].fillna(method="ffill", inplace=True)

def collect_pandas_frame_data(filename, fields, **kwargs):
    # previously generated cvs file?
    if re.match(".*\.csv$", filename):
        if args.filter_csv:
            return pd.read_csv(filename)[fields]
        else:
            return pd.read_csv(filename)

    # otherwise assume we're processing a .slp file
    frame_data = []
    frame_count = 0
    header = sorted(fields)
    iter_rec = iter(get_frame_record(filename,**kwargs))

    for sd in iter_rec:
        values = [sd_extract_field(sd,key) for key in header]
        frame_data.append(values)
        frame_count += 1

    return pd.DataFrame(frame_data,columns=header)

def get_all_timer_keys(filename):
    iter_rec = iter(get_frame_record(filename))
    sd = next(iter_rec)
    return [timer for timer in sd["Timers"]]

# Shorten 33,412 to 33K, etc., for simplified display.
# Input: float
# Output: string
def abbrev_number(f):
    if f <= 0.0:
        return "0"
    if f <= 1e3:
        return str(int(f))
    if f <= 1e6:
        return str(int(f/1e3))+"K"
    if f <= 1e9:
        return str(int(f/1e6))+"M"
    if f <= 1e12:
        return str(int(f/1e9))+"G"
    else:
        return str(int(f/1e12))+"T"

def get_outfit_spans(pd_data): 
    results = []
    results = []
    outfit_key = "Avatars.Self.OutfitName"
    arc_key="Avatars.Self.ARCCalculated"
    time_key="Timers.Frame.Time" 
    pd_data['span_num'] = (pd_data[arc_key].shift(1) != pd_data[arc_key]).astype(int).cumsum()
    grouped = pd_data.groupby([outfit_key,arc_key,'span_num'])
    if args.verbose:
        print "Grouped has",len(grouped),"groups"
    for name, group in grouped:
        #print name, len(group) 
        #print "group:", group
        num_frames = len(group)
        times = group[time_key]
        duration = sum(times)
        if (num_frames) > 200 and (duration > 10.0):
            low, high = np.percentile(times,5.0), np.percentile(times,95.0)
            outfit_rec = {"outfit": name[0], 
                          "arc": name[1],
                          "duration": duration,
                          "num_frames": num_frames,
                          "group": group,
                          "start_frame": min(group.index),
                          "avg": np.percentile(times, 50.0), #np.average(timespan.clip(low,high)), 
                          "std": np.std(times), 
                          "times": times}
            std_props = ["Avatars.Self.ARCCalculated",
                         "Avatars.Self.AttachmentTextures.material_texture_count",
                         "Avatars.Self.AttachmentTextures.material_texture_missing",
                         "Avatars.Self.AttachmentTextures.material_texture_mpixels",
                         "Avatars.Self.AttachmentTextures.texture_count",
                         "Avatars.Self.AttachmentTextures.texture_missing",
                         "Avatars.Self.AttachmentTextures.texture_mpixels",
                         "Derived.Avatar.Attachments.Count",
                         "Derived.Avatar.Attachments.triangles_high"]
            std_props.extend(["Derived.Avatar.Attachments." + key for key in bool_graphic_properties])
            std_props.extend(["Derived.Avatar.Attachments." + key for key in sum_graphic_properties])
            for f in std_props:
                outfit_rec[f] = group.iloc[0][f]
            #print outfit_rec
            results.append(outfit_rec)
    if args.verbose:
        print len(results),"groups found of sufficient duration and frame count"
    df =  pd.DataFrame(results)
    return df

def process_by_outfit(pd_data,cost_key="Avatars.Self.ARCCalculated"):
    time_key = "Timers.Frame.Time"
    outfit_spans = get_outfit_spans(pd_data)
    outfit_spans = outfit_spans.sort_values("avg")
    outfits_csv_filename = get_default_export_name(pd_data,"outfits")
    outfit_spans.to_csv(outfits_csv_filename, columns=outfit_spans.columns.difference(['group','times']))

    all_times = []
    num_rows = outfit_spans.shape[0]
    errorbars_low = []
    errorbars_high = []
    labels = []
    timeses = []

    for index, outfit_span in outfit_spans.iterrows():
        outfit = outfit_span["outfit"]
        avg = outfit_span["avg"]
        arc = outfit_span["arc"]
        times = outfit_span["times"]
        num_frames = outfit_span["num_frames"]
        timeses.append(times)
        all_times.extend(times)
        errorbar_low = (avg-np.percentile(times,25.0))
        errorbar_high = (np.percentile(times,75.0)-avg)
        errorbars_low.append(errorbar_low)
        errorbars_high.append(errorbar_high)
        label = outfit + " arc " + abbrev_number(arc) + " frames " + str(num_frames)
        labels.append(label)
        per_outfit_csv_filename = label.replace(" ","_").replace("*","") + ".csv"
        outfit_span["group"].to_csv(per_outfit_csv_filename)

    avgs = outfit_spans["avg"]
    costs = outfit_spans[cost_key]
    arcs = outfit_spans["arc"]
    if num_rows>1:
        try:
            corr = np.corrcoef([costs,avgs,arcs])
            print corr
            print "CORRELATION between",cost_key,"and",time_key,"is:",corr[0][1]
            columns = outfit_spans.columns.difference(['group','times','outfit'])
            subspans = outfit_spans[columns].T
            #print subspans.describe()
            print subspans.values
            allcorr = np.corrcoef(subspans.values)
            print "columns",len(columns),columns
            print allcorr
        except:
            print "CORCOEFF failed"
            raise

    plt.errorbar(costs, avgs, yerr=(errorbars_low, errorbars_high), fmt='o')
    for label, x, y in zip(labels,costs,avgs):
        plt.annotate(label, xy=(x,y))
    plt.gca().set_xlabel(cost_key)
    plt.gca().set_ylabel(time_key)
    plt.gcf().savefig("costs_vs_times.pdf", bbox_inches="tight")
    plt.clf()
    all_low, all_high = np.percentile(all_times,0), np.percentile(all_times,98.0)
    fig = plt.figure(figsize=(6, 2*num_rows))
    fig.subplots_adjust(wspace=1.0)
    for i, (times, label, avg_val) in enumerate(zip(timeses,labels, avgs)):
        #fig = plt.figure()
        ax = fig.add_subplot(num_rows,1,i+1)
        low, high = np.percentile(times,2.0), np.percentile(times,98.0)
        ax.hist(times, 100, label=label, range=(all_low,all_high), alpha=0.3)
        plt.title(label)
        plt.axvline(x=avg_val)
    plt.tight_layout()
    fig.savefig("times_histo_outfits.pdf")
        
def plot_time_series(pd_data,fields):
    time_key = "Timers.Frame.Time"
    print "plot_time_series",fields
    outfit_spans = get_outfit_spans(pd_data)
    for f in fields:
        #pd_data[f] = pd_data[f].clip(0,0.05)
        ax = pd_data.plot(y=f, alpha=0.3)
        for index, outfit_span in outfit_spans.iterrows():
            index = outfit_span["group"].index
            #print "times count",len(times),times
            x0 = min(index)
            x1 = max(index)
            y = outfit_span["avg"]
            arc = outfit_span["arc"]
            outfit = outfit_span["outfit"]
            print "annotate",outfit,(x0,x1),y
            plt.plot((x0,x1),(y,y),"b-")
            plt.text((x0+x1)/2, y, outfit + " arc " + abbrev_number(arc) , ha='center', va='bottom', rotation='vertical')
        ax.set_ylim([0,.1])
        fig = ax.get_figure()
        fig.savefig("time_series_" + f + ".pdf")

def compare_frames(a,b):
    fill_blanks(a)
    fill_blanks(b)
    print "Comparing two data frames"
    a_numeric = [col for col in a.columns.values if a[col].dtype=="float64"]
    b_numeric = [col for col in b.columns.values if b[col].dtype=="float64"]
    shared_columns = sorted(set(a_numeric).intersection(set(b_numeric)))
    print "compare_frames found",len(shared_columns),"shared columns"
    names = [col for col in shared_columns]
    mean_a = [a[col].mean() for col in shared_columns]
    mean_b = [b[col].mean() for col in shared_columns]
    abs_diff_mean = [abs(a[col].mean()-b[col].mean()) for col in shared_columns]
    diff_mean_pct = [(100.0*(b[col].mean()-a[col].mean())/a[col].mean() if a[col].mean()!=0.0 else 0.0) for col in shared_columns]
    compare_df = pd.DataFrame({"names": names, 
                               "mean_a": mean_a, 
                               "mean_b": mean_b, 
                               "abs_diff_mean": abs_diff_mean,
                               "diff_mean_pct": diff_mean_pct,
    })
    print compare_df.describe()
    compare_df.to_csv("compare.csv")
    return compare_df

def extract_percent(df, key="Timers.Frame.Time", low=0.0, high=100.0, filename="extract_percent.csv"):
    df.to_csv("percent_input.csv")
    result = df
    result["Avatars.Self.OutfitName"] = df["Avatars.Self.OutfitName"].fillna(method='ffill')
    result["Avatars.Self.ARCCalculated"] = df["Avatars.Self.ARCCalculated"].fillna(method='ffill')
    result = result[result[key] > result[key].quantile(low/100.0)]
    result = result[result[key] < result[key].quantile(high/100.0)]
    result.to_csv(filename)
    
if __name__ == "__main__":

    default_fields = [ "Timers.Frame.Time", 
                       "Session.UniqueHostID", 
                       "Session.UniqueSessionUUID", 
                       "Summary.Timestamp", 
                       "Avatars.Self.ARCCalculated", 
                       "Avatars.Self.OutfitName", 
                       "Avatars.Self.AttachmentSurfaceArea",
                       "Avatars.Self.AttachmentTextures.material_texture_count",
                       "Avatars.Self.AttachmentTextures.material_texture_missing",
                       "Avatars.Self.AttachmentTextures.material_texture_mpixels",
                       "Avatars.Self.AttachmentTextures.texture_count",
                       "Avatars.Self.AttachmentTextures.texture_missing",
                       "Avatars.Self.AttachmentTextures.texture_mpixels",
                       "Derived.Avatar.Attachments.Count",
                       "Derived.Avatar.Attachments.MeshCount",
                       "Derived.Avatar.Attachments.triangles_high",
    ]
    default_fields.extend(["Derived.Avatar.Attachments." + key for key in bool_graphic_properties])
    default_fields.extend(["Derived.Avatar.Attachments." + key for key in sum_graphic_properties])

    parser = argparse.ArgumentParser(description="analyze viewer performance files")
    parser.add_argument("--verbose", action="store_true", help="verbose flag")
    parser.add_argument("--summarize", action="store_true", help="show summary of results")
    parser.add_argument("--filter_csv", action="store_true", help="restrict to requested fields/timers when reading csv files too")
    parser.add_argument("--export", help="export results to specified file")
    parser.add_argument("--by_outfit", action="store_true", help="break results down based on active outfit")
    parser.add_argument("--plot_time_series", nargs="+", default=[], help="show timers by frame")
    parser.add_argument("--compare", help="compare infilename to specified file")
    parser.add_argument("infilename", help="name of performance or csv file", nargs="?", default="performance.slp")
    args = parser.parse_args()

    all_keys = ["Frame"]
    all_timers = sorted(["Timers." + key + ".Time" for key in all_keys])

    pd_data = collect_pandas_frame_data(args.infilename, default_fields)
    fill_blanks(pd_data)

    if args.compare:
        compare_data = collect_pandas_frame_data(args.compare, default_fields)
        fill_blanks(compare_data)
        compare_frames(pd_data, compare_data)
  
    export_file = export("auto", pd_data)
    if export_file is not None:
        pd_data = collect_pandas_frame_data(export_file, default_fields)
    else:
        raise Error() 

    process_by_outfit(pd_data)
    plot_time_series(pd_data, args.plot_time_series)
    print "done"