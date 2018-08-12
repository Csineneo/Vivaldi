// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.display;

import android.annotation.TargetApi;
import android.content.Context;
import android.graphics.Point;
import android.os.Build;
import android.view.Display;

import java.util.WeakHashMap;

/**
 * Chromium's object for android.view.Display. Instances of this object should be obtained
 * from WindowAndroid.
 * This class is designed to avoid leaks. It is ok to hold a strong ref of this class from
 * anywhere, as long as the corresponding WindowAndroids are destroyed. The observers are
 * held weakly so to not lead to leaks.
 */
public class DisplayAndroid {
    /**
     * DisplayAndroidObserver interface for changes to this Display.
     */
    public interface DisplayAndroidObserver {
        /**
         * Called whenever the screen orientation changes.
         *
         * @param orientation One of Surface.ROTATION_* values.
         */
        void onRotationChanged(int rotation);
    }

    private static final DisplayAndroidObserver[] EMPTY_OBSERVER_ARRAY =
            new DisplayAndroidObserver[0];

    private final int mSdkDisplayId;
    private final WeakHashMap<DisplayAndroidObserver, Object /* null */> mObservers;
    // Do NOT add strong references to objects with potentially complex lifetime, like Context.

    // Updated by updateFromDisplay.
    private final Point mSize;
    private final Point mPhysicalSize;
    private int mRotation;

    private static DisplayAndroidManager getManager() {
        return DisplayAndroidManager.getInstance();
    }

    // Internal implementation. Should not be called outside of UI.
    public static DisplayAndroid get(Context context) {
        Display display = DisplayAndroidManager.getDisplayFromContext(context);
        return getManager().getDisplayAndroid(display);
    }

    /**
     * @return Display height in physical pixels.
     */
    public int getDisplayHeight() {
        return mSize.y;
    }

    /**
     * @return Display width in physical pixels.
     */
    public int getDisplayWidth() {
        return mSize.x;
    }

    /**
     * @return Real physical display height in physical pixels. Or 0 if not supported.
     */
    public int getPhysicalDisplayHeight() {
        return mPhysicalSize.y;
    }

    /**
     * @return Real physical display width in physical pixels. Or 0 if not supported.
     */
    public int getPhysicalDisplayWidth() {
        return mPhysicalSize.x;
    }

    /**
     * @return current orientation. One of Surface.ORIENTATION_* values.
     */
    public int getRotation() {
        return mRotation;
    }

    /**
     * Add observer. Note repeat observers will be called only one.
     * Observers are held only weakly by Display.
     */
    public void addObserver(DisplayAndroidObserver observer) {
        mObservers.put(observer, null);
    }

    /**
     * Remove observer.
     */
    public void removeObserver(DisplayAndroidObserver observer) {
        mObservers.remove(observer);
    }

    /**
     * Toggle the accurate mode if it wasn't already doing so. The backend will
     * keep track of the number of times this has been called.
     */
    public static void startAccurateListening() {
        getManager().startAccurateListening();
    }

    /**
     * Request to stop the accurate mode. It will effectively be stopped only if
     * this method is called as many times as startAccurateListening().
     */
    public static void stopAccurateListening() {
        getManager().startAccurateListening();
    }

    /* package */ DisplayAndroid(Display display) {
        mSdkDisplayId = display.getDisplayId();
        mObservers = new WeakHashMap<>();
        mSize = new Point();
        mPhysicalSize = new Point();
        updateFromDisplay(display);
    }

    @TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
    /* package */ void updateFromDisplay(Display display) {
        display.getSize(mSize);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            display.getRealSize(mPhysicalSize);
        }

        int newRotation = display.getRotation();
        boolean rotationChanged = newRotation != mRotation;
        mRotation = newRotation;

        if (rotationChanged) {
            DisplayAndroidObserver[] observers = getObservers();
            for (DisplayAndroidObserver o : observers) {
                o.onRotationChanged(mRotation);
            }
        }
    }

    private DisplayAndroidObserver[] getObservers() {
        // Makes a copy to allow concurrent edit.
        return mObservers.keySet().toArray(EMPTY_OBSERVER_ARRAY);
    }
}
