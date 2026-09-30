from itertools import combinations
from chord_progression import (
    infer_chord_root,
    parse_chord,
    parse_mode,
    pitch_class_distance,
    tonal_stability,
)


# 现代音乐理论中的和弦情感色彩四维评估模型：
# 1.十二平均律中不同interval class的经验参数:
#   0=同音
#   1=小二度/大七度
#   2=大二度/小七度
#   3=小三度/大六度
#   4=大三度/小六度
#   5=纯四度/纯五度
#   6=三全音
# 2.consonance:越高越协和
# 3.valence:
#   -1=偏负向/阴暗
#    0=中性
#   +1=偏正向/明亮
# 4.tension:越高越紧张
# ex.调式介入修正（如离调音）

INTERVAL_PROFILE={
    0: {
        "consonance":1.00,
        "valence":0.10,
        "tension":0.00
    },
    1:{
        "consonance":0.08,
        "valence":-1.00,
        "tension":1.00
    },
    2:{
        "consonance":0.38,
        "valence":-0.35,
        "tension":0.72
    },
    3:{
        "consonance":0.62,
        "valence":-0.30,
        "tension":0.32
    },
    4:{
        "consonance":0.88,
        "valence":1.00,
        "tension":0.12
    },
    5:{
        "consonance":0.90,
        "valence":0.45,
        "tension":0.10
    },
    6:{
        "consonance":0.15,
        "valence":-0.75,
        "tension":0.92
    },
}


# 各调式的整体明暗倾向（仅用于小幅修正）
MODE_BRIGHTNESS={
    "Lydian":0.35,
    "Ionian":0.20,
    "Mixolydian":0.05,
    "Dorian":0.00,
    "Aeolian":-0.20,
    "Phrygian":-0.35,
    "Locrian":-0.50,
}


# I.获取和弦中所有音程
def get_intervals(chord):
    """
    输入pitch-class和弦：[0,4,7]
    得到其中所有两两音程：
        C-E=4
        C-G=5
        E-G=3
    返回：[4,5,3]
    """
    intervals=[]
    for a,b in combinations(chord,2):
        intervals.append(
            pitch_class_distance(a,b)
        )
    return intervals


# II.计算协和度
def calculate_consonance(intervals):
    """
    计算和弦整体协和度，映射为[0.0,1.0]
    """
    values=[
        INTERVAL_PROFILE[i]["consonance"]
        for i in intervals
    ]
    return sum(values)/len(values)


# III.计算紧张度
def calculate_tension(intervals):
    """
    紧张度不能简单取平均
    一个和弦中只要存在非常尖锐的：小二度/大七度/三全音
    即使其它音程很协和，听觉上仍然可能具有明显tension(紧张感)
    所以采用加权：65%-最紧张音程，35%-所有音程平均值
    """
    tensions=[
        INTERVAL_PROFILE[i]["tension"]
        for i in intervals
    ]
    average_tension=sum(tensions)/len(tensions)
    peak_tension=max(tensions)
    tension=0.65*peak_tension+0.35*average_tension
    return tension


# IV.音程情感
def calculate_interval_valence(intervals):
    """
    根据和弦内部所有音程估计valence
    例如，大三度倾向正向；小三度倾向负向；
    小二度、三全音等强烈不协和音程倾向降低valence
    映射并返回[-1.0,+1.0]
    """
    values=[
        INTERVAL_PROFILE[i]["valence"]
        for i in intervals
    ]
    return sum(values)/len(values)


# V.尝试检测 Major/Minor三度结构
def detect_third_character(chord):
    """
    先推断和弦根音，再检查根音上方的大三度（+4）或小三度（+3）。
    大三度返回 +1.0，小三度返回 -1.0；两者同时存在或都不存在时返回 0.0。
    """
    chord_set=set(chord)
    root=infer_chord_root(chord)
    has_major_third=(root+4)%12 in chord_set
    has_minor_third=(root+3)%12 in chord_set

    # 只用推断根音上方的三度判断大小调色彩；变化和弦同时含有两种三度时保持中性
    if has_major_third and not has_minor_third:
        return 1.0
    if has_minor_third and not has_major_third:
        return -1.0
    return 0.0


# VI.音级密集度
def calculate_cluster_density(intervals):
    """
    检测半音/全音簇
    大量相邻音级往往意味着roughness↑ / tension↑
    映射并返回[0.0~1.0]
    """
    cluster_count=sum(
        1
        for i in intervals
        if i<=2
    )
    return cluster_count/len(intervals)


# VII.“调式语境”
def calculate_mode_context(chord,tonic,mode_name,mode_intervals):
    """
    计算调式对单个和弦情感色彩的修正量
    ·调式整体明暗只做轻度修正；
    ·根音越不稳定，负向修正越强；
    ·调外音会带来最明显的负向修正，避免借用和弦被自身的大三度误判为明亮
    """
    mode_pitch_classes={
        (tonic+interval)%12
        for interval in mode_intervals
    }
    out_of_mode_count=sum(
        pc not in mode_pitch_classes
        for pc in chord
    )
    chromatic_ratio=out_of_mode_count/len(chord)

    root=infer_chord_root(chord)
    root_stability=tonal_stability(root,tonic,mode_intervals)
    mode_brightness=MODE_BRIGHTNESS[mode_name]

    adjustment=(
        0.25*mode_brightness
        -0.20*(1.0-root_stability)
        -0.90*chromatic_ratio
    )
    return {
        "mode_fit":1.0-chromatic_ratio,
        "mode_brightness":mode_brightness,
        "root_stability":root_stability,
        "adjustment":adjustment,
    }


# VIII.最终情感评分
def chord_emotion_score(mode_string:str,chord_string:str) -> float:
    """
    0~极阴暗/强烈负向
    50~中性/暧昧
    100~极明亮/强烈正向
    """
    tonic,mode_name,mode_intervals=parse_mode(mode_string)
    chord=parse_chord(chord_string)
    intervals=get_intervals(chord)
    consonance=calculate_consonance(intervals)
    tension=calculate_tension(intervals)
    interval_valence=calculate_interval_valence(intervals)
    third_character=detect_third_character(chord)
    cluster_density=calculate_cluster_density(intervals)
    mode_context=calculate_mode_context(
        chord,
        tonic,
        mode_name,
        mode_intervals,
    )


    # 赋权：
    # interval valence     45%
    # major/minor character 30%
    # consonance           15%
    # tension              -7%
    # cluster              -3%
    # mode context         调式明暗、根音稳定度、调外音比例
    # 最后把-1~+1映射到0~100
    raw_valence=(
        0.45*interval_valence
        +0.30*third_character
        +0.15*(2*consonance-1)
        -0.07*tension
        -0.03*cluster_density
        +mode_context["adjustment"]
    )

    # 防止越界检测
    raw_valence=max(-1.0,min(1.0,raw_valence))
    score=50.0*(raw_valence+1.0)
    return max(0.0,min(100.0,score))


# IX.情感标签（Optional）
def emotion_label(score):
    """
    数值区域映射为情感
    """
    if score<20:
        return "强烈阴暗/压迫"
    elif score<35:
        return "阴暗/紧张"
    elif score<45:
        return "忧郁/冷峻"
    elif score<55:
        return "中性/暧昧"
    elif score<70:
        return "温暖/平和"
    elif score<85:
        return "明亮/愉悦"
    else:
        return "强烈明亮/欢乐"


# X.综合分析（Optional）
def analyze_chord_emotion(mode_string,chord_string):
    """
    做 VST GUI时再用。比单独返回score更有用
    主接口chord_emotion_score()仍然只返回0~100
    """
    tonic,mode_name,mode_intervals=parse_mode(mode_string)
    chord=parse_chord(chord_string)
    intervals=get_intervals(chord)
    score=chord_emotion_score(mode_string,chord_string)
    consonance=calculate_consonance(intervals)
    tension=calculate_tension(intervals)
    mode_context=calculate_mode_context(
        chord,
        tonic,
        mode_name,
        mode_intervals,
    )
    return {
        "score":round(score,2),
        "emotion":emotion_label(score),
        "consonance":round(consonance*100,2),
        "tension":round(tension*100,2),
        "mode_fit":round(mode_context["mode_fit"]*100,2),
        "root_stability":round(mode_context["root_stability"]*100,2),
        "mode_adjustment":round(mode_context["adjustment"]*50,2),
    }


# XI.主程序/主函数
if __name__=="__main__":
    mode_string=input().strip()
    chord_string=input().strip()
    try:
        result=analyze_chord_emotion(mode_string,chord_string)
        print(f"{result['score']:.2f}")
    except ValueError as e:
        print(e)
